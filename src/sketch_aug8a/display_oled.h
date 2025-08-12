#ifndef DISPLAY_OLED_H
#define DISPLAY_OLED_H

#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

static Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
static uint8_t oledAddress = 0x3C;

inline void OLED_I2C_Begin() {
  Wire.begin(I2C_SDA, I2C_SCL);
}

inline uint8_t I2C_ScanAndPickOLED() {
  Serial.println("🔎 Quet I2C...");
  uint8_t chosen = 0x00;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("  • Thiet bi I2C: 0x");
      if (a < 16) Serial.print("0");
      Serial.println(a, HEX);
      if (a == 0x3C || a == 0x3D) chosen = a;
    }
    delay(2);
  }
  if (!chosen) { Serial.println("⚠️ Khong thay 0x3C/0x3D, dung 0x3C mac dinh."); chosen = 0x3C; }
  Serial.print("✅ Chon dia chi OLED: 0x"); Serial.println(chosen, HEX);
  return chosen;
}

inline void OLED_Init() {
  oledAddress = I2C_ScanAndPickOLED();
  if (!display.begin(oledAddress, true)) {
    Serial.println("❌ Khong khoi tao duoc OLED (SH1106). Treo an toan.");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("OLED SH1106 OK");
  display.display();
  delay(400);
}

inline const __FlashStringHelper* LevelText(int lv) {
  if (lv==0) return F("OK");
  if (lv==1) return F("CANH BAO");
  return F("NGUY HIEM");
}

// Render tất cả thông tin như bản gốc
inline void OLED_Render(const String& timeStr, const EnvState& st) {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.print(F("Time: "));
  display.println(timeStr);

  display.setCursor(0, 12);
  if (!isnan(st.temp)) {
    display.print(F("Temp: "));
    display.print(st.temp, 1);
    display.println(F(" C"));
  } else {
    display.println(F("Temp: Loi DHT"));
  }

  display.setCursor(0, 24);
  if (!isnan(st.hum)) {
    display.print(F("Hum : "));
    display.print(st.hum, 1);
    display.print(F(" %"));
  } else {
    display.println(F("Hum : Loi DHT"));
  }

  display.setCursor(0, 36);
  display.print(F("Gas:"));
  display.print(st.gas);
  display.print(F("  Dust:"));
  display.print(st.dust,1);
  display.println(F("mg/m3"));

  display.setCursor(0, 48);
  display.print(F("Status: "));
  display.println(LevelText(st.level));

  display.display();
}

#endif
