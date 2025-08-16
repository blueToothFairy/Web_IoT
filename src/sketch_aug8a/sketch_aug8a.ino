#include "config.h"
#include "levels.h"
#include "sensors.h"
#include "display_oled.h"
#include "buzzer.h"
#include "wifi_ntp_http.h"

void setup() {
  Serial.begin(115200);
  Serial.println("🚨 Khoi dong he thong (NO MQTT, modular .h)");

  // Phần cứng cơ bản
  pinMode(BUZZER, OUTPUT);
  pinMode(G3_PIN, OUTPUT);        // LED IR GP2Y1010
  digitalWrite(G3_PIN, HIGH);     // tắt LED bụi ban đầu

  //LED
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);

  // I2C + OLED
  OLED_I2C_Begin();
  OLED_Init();                    // auto-scan 0x3C/0x3D và begin()

  // Wi‑Fi + NTP
  WIFI_Begin();

  if (is_WIFI_Connected()) {
    NTP_Ensure();
  }

  // Cảm biến
  Sensors_Init();

  // HTTP server
  HTTP_Begin();

  Serial.println("✅ He thong san sang (gio thuc).");
}

void loop() {
  static uint32_t last1Hz = 0;
  
  if (!is_WIFI_Connected()) {
    Serial.print("Reconnecting to WiFi...");
    connectWifi();
  }
  HTTP_Loop();

  if (millis() - last1Hz >= UPDATE_MS) {
    last1Hz = millis();

    // Đọc & xử lý sensor + phân loại
    Sensors_Update1Hz();

    // Buzzer theo mức tổng
    const EnvState& st = Sensors_GetState();
    Buzzer_Update((Level)st.level);

    // OLED + thời gian hiện tại
    String tstr = Now_String();
    OLED_Render(tstr, st);
  }
}