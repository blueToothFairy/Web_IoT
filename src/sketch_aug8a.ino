// ======================= ESP32 ENV MONITOR (NO MQTT) =======================
// OLED 1.3" I2C (SH1106 128x64) + DHT22 + MQ-2 + Dust (GP2Y1010) + Buzzer
// - Wi-Fi (2.4GHz) + NTP giờ thực (bắt buộc sync mới chạy đo)
// - Serial log JSON mỗi 2 giây
// - Tự quét địa chỉ I2C của OLED (0x3C hoặc 0x3D)
// ===========================================================================

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <time.h>

// -------------------- PINOUT / HARDWARE --------------------
#define DHTPIN         15
#define DHTTYPE        DHT22
#define BUZZER         13
#define MQ2_PIN        34

// Dust sensor GP2Y1010:
// G3_PIN: LED IR control (LOW ~0.28ms), G5_PIN: Vo analog -> nên dùng ADC1 khi bật Wi-Fi
#define G3_PIN         23     // LED control
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

// -------------------- THRESHOLDS --------------------
const float TEMP_THRESHOLD = 40.0;   // °C
const int   MQ2_THRESHOLD  = 1800;   // 0–4095
const int   DUST_THRESHOLD = 1600;   // raw ADC (tùy chỉnh)

// -------------------- WIFI + NTP --------------------
// Giữ nguyên SSID như bạn yêu cầu:
const char* ssid     = "Trang thu_5G";   // Router của bạn phải có băng 2.4GHz cùng SSID
const char* password = "19631965";

// Dùng nhiều NTP để tăng xác suất sync
const char* ntpServers[] = {
  "vn.pool.ntp.org", "asia.pool.ntp.org", "time.google.com", "pool.ntp.org"
};
const long  gmtOffset_sec      = 7 * 3600; // GMT+7
const int   daylightOffset_sec = 0;

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

// -------------------- WIFI + NTP (bắt buộc đồng bộ) --------------------
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
    Serial.println("\n⛔ Không kết nối được Wi-Fi. Kiểm tra SSID/mật khẩu & băng 2.4GHz.");
    safeHalt();
  }
}

void ensureNTP() {
  // yêu cầu đồng bộ giờ thực trước khi chạy
  for (const char* server : ntpServers) {
    Serial.print("Đồng bộ NTP: "); Serial.println(server);
    configTime(gmtOffset_sec, daylightOffset_sec, server);
    unsigned long t0 = millis();
    while (millis() - t0 < 10000) { // chờ 10s mỗi server
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

// -------------------- READ DUST --------------------
int readDustRaw() {
  digitalWrite(G3_PIN, LOW);             // LED ON
  delayMicroseconds(280);
  int v = analogRead(G5_PIN);            // ADC1, ổn với Wi-Fi
  delayMicroseconds(40);
  digitalWrite(G3_PIN, HIGH);            // LED OFF
  delayMicroseconds(9680);               // chu kỳ ~10ms
  return v; // 0..4095
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
  Serial.println("✅ He thong san sang (gio thuc).");
}

// -------------------- LOOP --------------------
void loop() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  bool dht_ok = !(isnan(temp) || isnan(hum));

  int mq2Value = analogRead(MQ2_PIN);
  int dustVal  = readDustRaw();

  bool tempHigh = (dht_ok && temp > TEMP_THRESHOLD);
  bool gasHigh  = (mq2Value > MQ2_THRESHOLD);
  bool dustHigh = (dustVal  > DUST_THRESHOLD);

  if (tempHigh || gasHigh || dustHigh) {
    tone(BUZZER, 1500); delay(200); noTone(BUZZER);
  }

  String tstr = nowString();
  String payload = "{\"time\":\"" + tstr + "\","
                   "\"temp\":" + (dht_ok ? String(temp,1) : "null") + ","
                   "\"hum\":"  + (dht_ok ? String(hum,1)  : "null") + ","
                   "\"gas\":"  + String(mq2Value) + ","
                   "\"dust\":" + String(dustVal) + "}";
  Serial.println(payload);

  display.clearDisplay();
  display.setCursor(0, 0);  display.print("Time: "); display.println(tstr);

  display.setCursor(0, 12);
  if (dht_ok) { display.print("Temp: "); display.print(temp,1); display.println(" C"); }
  else         { display.println("Temp: Loi DHT"); }

  display.setCursor(0, 24);
  if (dht_ok) { display.print("Hum : "); display.print(hum,1);  display.println(" %"); }
  else         { display.println("Hum : Loi DHT"); }

  display.setCursor(0, 36); display.print("Gas : ");  display.println(mq2Value);
  display.setCursor(0, 48); display.print("Dust: ");  display.println(dustVal);

  display.display();
  delay(2000);
}
