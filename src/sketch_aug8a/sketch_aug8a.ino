// ======================= ESP32 ENV MONITOR (NO MQTT) =======================
// OLED 1.3" I2C (SH1106 128x64) + DHT22 + MQ-2 + Dust (GP2Y1010) + Buzzer
// - Wi-Fi (2.4GHz) + NTP giờ thực (bắt buộc sync)
// - WebServer HTTP (cổng 80): /status (JSON), /on, /off, /test
// - Serial log JSON mỗi ~1 giây
// - Tự quét địa chỉ I2C của OLED (0x3C hoặc 0x3D)
// ===========================================================================

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

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
const char* ssid     = "Trang thu_5G";   // lưu ý: ESP32 chỉ vào 2.4 GHz
const char* password = "19631965";

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
const int   DUST_WARN  = 700;    // ADC
const int   DUST_DANG  = 1200;   // ADC

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
float g_temp = NAN, g_hum = NAN;
int   g_mq2 = 0, g_dust = 0;
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

void wifiConnect(unsigned long timeout_ms = 20000) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Kết nối Wi-Fi ");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Wi-Fi OK");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⛔ Không kết nối được Wi-Fi. Kiểm tra SSID/mật khẩu & 2.4GHz.");
    safeHalt();
  }
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
int readDustRaw() {
  digitalWrite(G3_PIN, LOW);             // LED ON
  delayMicroseconds(280);
  int v = analogRead(G5_PIN);            // ADC1
  delayMicroseconds(40);
  digitalWrite(G3_PIN, HIGH);            // LED OFF
  delayMicroseconds(9680);               // chu kỳ ~10ms
  return v; // 0..4095
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
  js += "\"dust\":" + String(g_dust) + ",";
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

  wifiConnect();
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
  server.handleClient();   // xử lý HTTP liên tục

  if (millis() - lastUpd < UPDATE_MS) return;
  lastUpd = millis();

  // ===== ĐỌC CẢM BIẾN =====
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  bool dht_ok = !(isnan(temp) || isnan(hum));

  int mq2Raw  = analogRead(MQ2_PIN);
  int dustRaw = readDustRaw();

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
  Level lvDust = levelWithHysInt((int)emaDust, DUST_WARN, DUST_DANG, DUST_HYS, prevDust);

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
  g_dust  = (int)emaDust;
  g_level = (int)lvAll;

  // ===== SERIAL JSON =====
  String tstr = nowString();
  String payload = "{\"time\":\"" + tstr + "\","
                   "\"temp\":" + (dht_ok ? String(emaTemp,1) : "null") + ","
                   "\"hum\":"  + (dht_ok ? String(emaHum,1)  : "null") + ","
                   "\"gas\":"  + String((int)emaGas) + ","
                   "\"dust\":" + String((int)emaDust) + ","
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
  display.println((int)emaDust);

  // Dòng trạng thái môi trường
  display.setCursor(0, 48);
  if (lvAll == LV_OK)       display.println("Status: OK");
  else if (lvAll == LV_WARN)display.println("Status: CANH BAO");
  else                      display.println("Status: NGUY HIEM");

  display.display();

  // Lưu mức cho hysteresis lần sau
  prevTemp = lvTemp; prevHum = lvHum; prevGas = lvGas; prevDust = lvDust;
}
