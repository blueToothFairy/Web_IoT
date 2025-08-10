#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <EEPROM.h>

// -------------------- PINOUT / HARDWARE --------------------
#define DHTPIN         15
#define DHTTYPE        DHT22
#define BUZZER         13
#define MQ2_PIN        34

// Dust sensor GP2Y1010:
#define G3_PIN         23     // LED IR control
#define G5_PIN         35     // Vo analog (ADC1)

// I2C pins for ESP32
#define I2C_SDA        21
#define I2C_SCL        22

// OLED SH1106
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
uint8_t oledAddress = 0x3C;

// -------------------- SENSORS --------------------
DHT dht(DHTPIN, DHTTYPE);

// -------------------- WIFI + NTP --------------------
String saved_ssid = "";
String saved_password = "";

const char* ntpServers[] = {
  "vn.pool.ntp.org", "asia.pool.ntp.org", "time.google.com", "pool.ntp.org"
};
const long  gmtOffset_sec      = 7 * 3600; // GMT+7
const int   daylightOffset_sec = 0;

// -------------------- SAFE THRESHOLDS (nhạy hơn) --------------------
const float TEMP_WARN  = 33.0;   // °C
const float TEMP_DANG  = 36.0;   // °C
const float HUM_LOW    = 35.0;   // %
const float HUM_HIGH   = 80.0;   // %
const int   MQ2_WARN   = 600;    // ADC
const int   MQ2_DANG   = 1000;   // ADC
const float   DUST_WARN  = 0.036;    // mg/m3
const float   DUST_DANG  = 0.056;   // mg/m3

// Hysteresis (khi hạ mức)
const float TEMP_HYS   = 0.8;    // 80% ngưỡng
const float HUM_HYS    = 0.95;
const float GAS_HYS    = 0.9;
const float DUST_HYS   = 0.9;

// EMA (làm mượt)
float emaTemp = NAN, emaHum = NAN;
float emaGas = 0, emaDust = 0;
const float EMA_ALPHA = 0.3f;    // 0.2–0.4 là hợp lý

// Web server
WebServer server(80);

// Lưu giá trị gần nhất cho /status
enum Level { LV_OK=0, LV_WARN=1, LV_DANG=2 };
float g_temp = NAN, g_hum = NAN, g_dust = 0;
int   g_mq2 = 0;
int   g_level = 0;  // 0 OK, 1 WARN, 2 DANG

// Nhịp cập nhật
const uint32_t UPDATE_MS = 1000;

// -------------------- UTILS --------------------
void safeHalt() { while (true) { delay(1000); } }

uint8_t scanI2CAndPickOLED() {
  Serial.println("🔎 Quét I2C...");
  uint8_t chosen = 0x00;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("  • Thiết bị I2C: 0x");
      if (a < 16) Serial.print("0");
      Serial.println(a, HEX);
      if (a == 0x3C || a == 0x3D) chosen = a;
    }
    delay(2);
  }
  if (!chosen) { Serial.println("⚠️ Không thấy 0x3C/0x3D, dùng 0x3C mặc định."); chosen = 0x3C; }
  Serial.print("✅ Chọn địa chỉ OLED: 0x"); Serial.println(chosen, HEX);
  return chosen;
}

void ensureNTP() {
  for (const char* serverN : ntpServers) {
    Serial.print("Đồng bộ NTP: "); Serial.println(serverN);
    configTime(gmtOffset_sec, daylightOffset_sec, serverN);
    unsigned long t0 = millis();
    while (millis() - t0 < 10000) {
      struct tm ti;
      if (getLocalTime(&ti)) {
        Serial.println("🕒 NTP OK");
        return;
      }
      delay(500); Serial.print(".");
    }
    Serial.println("\n⚠️ Thử server khác...");
  }
  Serial.println("⛔ Không đồng bộ được NTP. Kiểm tra mạng/UDP 123.");
  safeHalt();
}

String nowString() {
  struct tm ti;
  if (getLocalTime(&ti)) {
    char buf[20];
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return String(buf);
  }
  return "NTP-syncing";
}

// -------------------- DUST READ --------------------
float readDustDensity() {
  digitalWrite(G3_PIN, LOW);             // LED ON
  delayMicroseconds(280);
  int v = analogRead(G5_PIN);            // ADC1
  delayMicroseconds(40);
  digitalWrite(G3_PIN, HIGH);            // LED OFF
  delayMicroseconds(9680);               // chu kỳ ~10ms
  
  float vOut = v * 3.3 / 4095.0;
  return (0.17 * vOut - 0.1 < 0) ? 0 : (0.17 * vOut - 0.1);
}

// -------------------- LEVEL/HYSTERESIS --------------------

Level levelWithHysFloat(float v, float warn, float dang, float hys, Level prev) {
  if (isnan(v)) return prev;
  if (prev == LV_DANG) {
    if (v < dang*hys) return LV_WARN;
    return LV_DANG;
  }
  if (prev == LV_WARN) {
    if (v < warn*hys) return LV_OK;
    if (v >= dang)    return LV_DANG;
    return LV_WARN;
  }
  if (v >= dang) return LV_DANG;
  if (v >= warn) return LV_WARN;
  return LV_OK;
}

Level levelWithHysInt(int v, int warn, int dang, float hys, Level prev) {
  if (prev == LV_DANG) {
    if (v < (int)(dang*hys)) return LV_WARN;
    return LV_DANG;
  }
  if (prev == LV_WARN) {
    if (v < (int)(warn*hys)) return LV_OK;
    if (v >= dang)           return LV_DANG;
    return LV_WARN;
  }
  if (v >= dang) return LV_DANG;
  if (v >= warn) return LV_WARN;
  return LV_OK;
}

Level prevTemp=LV_OK, prevHum=LV_OK, prevGas=LV_OK, prevDust=LV_OK;

// -------------------- HTTP HANDLERS --------------------
void handleOn() {
  tone(BUZZER, 1500); delay(200); noTone(BUZZER);
  server.send(200, "text/plain", "OK: buzzer on (beep)");
}
void handleOff() {
  noTone(BUZZER);
  server.send(200, "text/plain", "OK: buzzer off");
}
void handleTest() {
  tone(BUZZER, 2000); delay(150); noTone(BUZZER);
  server.send(200, "text/plain", "OK: test beep");
}
String envLevelText(int lv) {
  if (lv==0) return "OK";
  if (lv==1) return "CANH BAO";
  return "NGUY HIEM";
}
String jsonStatus() {
  String tstr = nowString();
  String js = "{\"time\":\"" + tstr + "\",";
  if (isnan(g_temp) || isnan(g_hum)) {
    js += "\"temp\":null,\"hum\":null,";
  } else {
    js += "\"temp\":" + String(g_temp,1) + ",\"hum\":" + String(g_hum,1) + ",";
  }
  js += "\"gas\":" + String(g_mq2) + ",";
  js += "\"dust\":" + String(g_dust,1) + ",";
  js += "\"level\":" + String(g_level) + ",";
  js += "\"level_text\":\"" + envLevelText(g_level) + "\"}";
  return js;
}
void handleStatus() {
  server.send(200, "application/json", jsonStatus());
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  Serial.println("🚨 Khởi động hệ thống (NO MQTT)");

  pinMode(BUZZER, OUTPUT);
  pinMode(G3_PIN, OUTPUT);
  digitalWrite(G3_PIN, HIGH); // tắt LED bụi ban đầu
  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);
  oledAddress = scanI2CAndPickOLED();
  if (!display.begin(oledAddress, true)) {
    Serial.println("❌ Không khởi tạo được OLED (SH1106).");
    safeHalt();
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("OLED SH1106 OK");
  display.display();
  delay(400);

  EEPROM.begin(200);
  loadWifiConfig();
  if (saved_ssid.length() > 0) {
    Serial.println("Dang ket noi WiFi: " + saved_ssid);
    connectWifi();
  } else {
    Serial.println("Chua co cau hinh WiFi, vao che do setup");
    startConfigMode();
  }
  ensureNTP();

  // HTTP routes
  server.on("/on",     handleOn);
  server.on("/off",    handleOff);
  server.on("/test",   handleTest);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("🌐 HTTP server: http://" + WiFi.localIP().toString());

  Serial.println("✅ He thong san sang (gio thuc).");
}

// -------------------- LOOP --------------------
void loop() {
  static uint32_t lastUpd = 0;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Reconnecting to WiFi...");
    connectWifi();
  }
  server.handleClient();   // xử lý HTTP liên tục

  if (millis() - lastUpd < UPDATE_MS) return;
  lastUpd = millis();

  // ===== ĐỌC CẢM BIẾN =====
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  bool dht_ok = !(isnan(temp) || isnan(hum));

  int mq2Raw  = analogRead(MQ2_PIN);
  float dustRaw = readDustDensity();

  // EMA (làm mượt)
  if (dht_ok) {
    emaTemp = isnan(emaTemp) ? temp : EMA_ALPHA*temp + (1-EMA_ALPHA)*emaTemp;
    emaHum  = isnan(emaHum)  ? hum  : EMA_ALPHA*hum  + (1-EMA_ALPHA)*emaHum;
  }
  emaGas  = (emaGas==0)  ? mq2Raw  : EMA_ALPHA*mq2Raw  + (1-EMA_ALPHA)*emaGas;
  emaDust = (emaDust==0) ? dustRaw : EMA_ALPHA*dustRaw + (1-EMA_ALPHA)*emaDust;

  // ===== PHÂN LOẠI =====
  Level lvTemp = prevTemp, lvHum = prevHum;
  if (dht_ok) {
    lvTemp = levelWithHysFloat(emaTemp, TEMP_WARN, TEMP_DANG, TEMP_HYS, prevTemp);

    // Độ ẩm: cảnh báo nếu < HUM_LOW hoặc > HUM_HIGH
    float humSeverity = 0.0f;
    if (emaHum < HUM_LOW)  humSeverity = HUM_LOW - emaHum;   // thấp
    if (emaHum > HUM_HIGH) humSeverity = emaHum - HUM_HIGH;  // cao
    float humWarnGap = 5.0f;   // lệch 5%: warn
    float humDangGap = 10.0f;  // lệch 10%: danger
    Level target = LV_OK;
    if (humSeverity >= humDangGap) target = LV_DANG;
    else if (humSeverity >= humWarnGap) target = LV_WARN;
    if (prevHum == LV_DANG && humSeverity < humDangGap*HUM_HYS) target = LV_WARN;
    if (prevHum == LV_WARN && humSeverity < humWarnGap*HUM_HYS) target = LV_OK;
    lvHum = target;
  }

  Level lvGas  = levelWithHysInt((int)emaGas,  MQ2_WARN,  MQ2_DANG,  GAS_HYS,  prevGas);
  Level lvDust = levelWithHysFloat(emaDust, DUST_WARN, DUST_DANG, DUST_HYS, prevDust);

  // Tổng mức hệ thống
  Level lvAll = (Level)max((int)lvTemp, max((int)lvHum, max((int)lvGas, (int)lvDust)));

  // ===== CÒI THEO MỨC =====
  static uint32_t lastBeep=0;
  if (lvAll == LV_DANG) {
    if (millis() - lastBeep > 2000) {
      for (int i=0;i<3;i++){ tone(BUZZER, 2000); delay(120); noTone(BUZZER); delay(120); }
      lastBeep = millis();
    }
  } else if (lvAll == LV_WARN) {
    if (millis() - lastBeep > 3000) {
      tone(BUZZER, 1500); delay(150); noTone(BUZZER);
      lastBeep = millis();
    }
  } else {
    noTone(BUZZER);
  }

  // ===== CẬP NHẬT GIÁ TRỊ CHO /status =====
  g_temp  = dht_ok ? emaTemp : NAN;
  g_hum   = dht_ok ? emaHum  : NAN;
  g_mq2   = (int)emaGas;
  g_dust  = emaDust;
  g_level = (int)lvAll;

  // ===== SERIAL JSON =====
  String tstr = nowString();
  String payload = "{\"time\":\"" + tstr + "\","
                   "\"temp\":" + (dht_ok ? String(emaTemp,1) : "null") + ","
                   "\"hum\":"  + (dht_ok ? String(emaHum,1)  : "null") + ","
                   "\"gas\":"  + String((int)emaGas) + ","
                   "\"dust\":" + String(emaDust,1) + ","
                   "\"level\":" + String((int)lvAll) + "}";
  Serial.println(payload);

  // ===== OLED =====
  display.clearDisplay();
  display.setCursor(0, 0);  
  display.print("Time: "); 
  display.println(tstr);

  display.setCursor(0, 12);
  if (dht_ok) { 
    display.print("Temp: "); 
    display.print(emaTemp,1); 
    display.println(" C"); 
  } else { 
    display.println("Temp: Loi DHT"); 
  }

  display.setCursor(0, 24);
  if (dht_ok) { 
    display.print("Hum : "); 
    display.print(emaHum,1);  
    display.println(" %"); 
  } else { 
    display.println("Hum : Loi DHT"); 
  }

  // Gộp Gas và Dust trên cùng 1 dòng
  display.setCursor(0, 36);
  display.print("Gas: ");  
  display.print((int)emaGas);
  display.print("  Dust: ");  
  display.print(emaDust,1);
  display.println("mg/m3");

  // Dòng trạng thái môi trường
  display.setCursor(0, 48);
  if (lvAll == LV_OK)       display.println("Status: OK");
  else if (lvAll == LV_WARN)display.println("Status: CANH BAO");
  else                      display.println("Status: NGUY HIEM");

  display.display();

  // Lưu mức cho hysteresis lần sau
  prevTemp = lvTemp; prevHum = lvHum; prevGas = lvGas; prevDust = lvDust;
}

void loadWifiConfig() {
  int ssid_len = EEPROM.read(0);
  if (ssid_len > 0 && ssid_len < 32) {
    for (int i = 0; i < ssid_len; i++) {
      saved_ssid += char(EEPROM.read(1 + i));
    }
  }
  
  int pass_len = EEPROM.read(50);
  if (pass_len >= 0 && pass_len < 64) {
    for (int i = 0; i < pass_len; i++) {
      saved_password += char(EEPROM.read(51 + i));
    }
  }
  
  Serial.println("WiFi da luu: " + saved_ssid);
}

// Lưu WiFi vào EEPROM
void saveWifiConfig(String ssid, String password) {
  for (int i = 0; i < 150; i++) {
    EEPROM.write(i, 0);
  }
  
  EEPROM.write(0, ssid.length());
  for (int i = 0; i < ssid.length(); i++) {
    EEPROM.write(1 + i, ssid[i]);
  }
  
  EEPROM.write(50, password.length());
  for (int i = 0; i < password.length(); i++) {
    EEPROM.write(51 + i, password[i]);
  }
  
  EEPROM.commit();
  Serial.println("Da luu WiFi: " + ssid);
}

// Kết nối WiFi
void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
  
  Serial.print("Dang ket noi");
  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 20) {
    delay(500);
    Serial.print(".");
    count++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("Ket noi thanh cong!");
    Serial.println("IP: " + WiFi.localIP().toString());
    setupNormalServer();
  } else {
    Serial.println("");
    Serial.println("Ket noi that bai! Chuyen sang che do cau hinh");
    startConfigMode();
  }
}

// Chế độ cấu hình
void startConfigMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SafeSense360", "12345678");
  
  Serial.println("");
  Serial.println("WiFi: SafeSense360");
  Serial.println("Pass: 12345678");
  Serial.println("Truy cap: http://192.168.4.1");
  
  server.on("/", []() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>WiFi Config</title>";
    html += "<meta charset='UTF-8'>";
    html += "<style>";
    html += "body{font-family:Arial;max-width:400px;margin:50px auto;padding:20px;}";
    html += "input,button{width:100%;padding:10px;margin:10px 0;font-size:16px;}";
    html += "button{background:#4CAF50;color:white;border:none;cursor:pointer;}";
    html += ".network{background:#f0f0f0;padding:10px;margin:5px 0;cursor:pointer;}";
    html += "</style></head><body>";
    html += "<h2>WiFi Configuration</h2>";
    html += "<button onclick='scanWifi()'>Scan WiFi</button>";
    html += "<div id='networks'></div>";
    html += "<form onsubmit='saveWifi(event)'>";
    html += "<input type='text' id='ssid' placeholder='WiFi' required>";
    html += "<input type='password' id='password' placeholder='Password'>";
    html += "<button type='submit'>Connect</button>";
    html += "</form>";
    html += "<div id='status'></div>";
    html += "<script>";
    html += "function scanWifi(){";
    html += "document.getElementById('status').innerHTML='Scanning...';";
    html += "fetch('/scan').then(r=>r.text()).then(data=>{";
    html += "document.getElementById('networks').innerHTML=data;";
    html += "document.getElementById('status').innerHTML='';";
    html += "});}";
    html += "function selectWifi(ssid){";
    html += "document.getElementById('ssid').value=ssid;}";
    html += "function saveWifi(e){";
    html += "e.preventDefault();";
    html += "const ssid=document.getElementById('ssid').value;";
    html += "const pass=document.getElementById('password').value;";
    html += "document.getElementById('status').innerHTML='...';";
    html += "fetch('/save',{";
    html += "method:'POST',";
    html += "headers:{'Content-Type':'application/x-www-form-urlencoded'},";
    html += "body:'ssid='+ssid+'&password='+pass";
    html += "}).then(r=>r.text()).then(data=>{";
    html += "document.getElementById('status').innerHTML=data;";
    html += "});}";
    html += "scanWifi();";
    html += "</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  server.on("/scan", []() {
    Serial.println("Dang quet mang WiFi...");
    String html = "";
    int n = WiFi.scanNetworks();
    
    if (n > 0) {
      for (int i = 0; i < n; i++) {
        html += "<div class='network' onclick='selectWifi(\"" + WiFi.SSID(i) + "\")'>";
        html += WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)";
        html += "</div>";
      }
    } else {
      html = "<p>Khong tim thay mang WiFi nao</p>";
    }
    
    server.send(200, "text/html", html);
  });
  
  // Lưu cấu hình WiFi
  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    Serial.println("Nhan cau hinh moi:");
    Serial.println("SSID: " + ssid);
    Serial.print("Password: ");
    if (password.length() > 0) {
      Serial.println("***");
    } else {
      Serial.println("(trong)");
    }
    
    saveWifiConfig(ssid, password);
    saved_ssid = ssid;
    saved_password = password;
    
    server.send(200, "text/html", "<h3>Successfully connected!<br>Restarting...</h3>");
    
    delay(3000);
    ESP.restart();
  });
  
  server.begin();
  Serial.println("Web server da khoi dong!");
}

// Chế độ bình thường
void setupNormalServer() {
  server.on("/", []() {
    String html = "<html><head><title>ESP32 WiFi</title></head><body>";
    html += "<h2>Connected to WiFi!</h2>";
    html += "<p><b>Wifi:</b> " + WiFi.SSID() + "</p>";
    html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
    html += "<p><b>dBm:</b> " + String(WiFi.RSSI()) + " dBm</p>";
    html += "<br><a href='/config'><button>Switch Wifi</button></a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
  // Trang cấu hình (có thể truy cập khi đã kết nối)
  server.on("/config", []() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>WiFi Config</title>";
    html += "<meta charset='UTF-8'>";
    html += "<style>";
    html += "body{font-family:Arial;max-width:400px;margin:50px auto;padding:20px;}";
    html += "input,button{width:100%;padding:10px;margin:10px 0;font-size:16px;}";
    html += "button{background:#4CAF50;color:white;border:none;cursor:pointer;}";
    html += ".network{background:#f0f0f0;padding:10px;margin:5px 0;cursor:pointer;}";
    html += "</style></head><body>";
    html += "<h2>WiFi Configuration</h2>";
    html += "<button onclick='scanWifi()'>Scan WiFi</button>";
    html += "<div id='networks'></div>";
    html += "<form onsubmit='saveWifi(event)'>";
    html += "<input type='text' id='ssid' placeholder='WiFi' required>";
    html += "<input type='password' id='password' placeholder='Password'>";
    html += "<button type='submit'>Connect</button>";
    html += "</form>";
    html += "<div id='status'></div>";
    html += "<script>";
    html += "function scanWifi(){";
    html += "document.getElementById('status').innerHTML='Scanning...';";
    html += "fetch('/scan').then(r=>r.text()).then(data=>{";
    html += "document.getElementById('networks').innerHTML=data;";
    html += "document.getElementById('status').innerHTML='';";
    html += "});}";
    html += "function selectWifi(ssid){";
    html += "document.getElementById('ssid').value=ssid;}";
    html += "function saveWifi(e){";
    html += "e.preventDefault();";
    html += "const ssid=document.getElementById('ssid').value;";
    html += "const pass=document.getElementById('password').value;";
    html += "document.getElementById('status').innerHTML='...';";
    html += "fetch('/save',{";
    html += "method:'POST',";
    html += "headers:{'Content-Type':'application/x-www-form-urlencoded'},";
    html += "body:'ssid='+ssid+'&password='+pass";
    html += "}).then(r=>r.text()).then(data=>{";
    html += "document.getElementById('status').innerHTML=data;";
    html += "});}";
    html += "scanWifi();";
    html += "</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  server.on("/scan", []() {
    String html = "";
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      html += "<div class='network' onclick='selectWifi(\"" + WiFi.SSID(i) + "\")'>";
      html += WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)";
      html += "</div>";
    }
    server.send(200, "text/html", html);
  });
  
  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    saveWifiConfig(ssid, password);
    server.send(200, "text/html", "<h3>Successfully connected! Restarting...</h3>");
    delay(2000);
    ESP.restart();
  });
  
  server.begin();
}