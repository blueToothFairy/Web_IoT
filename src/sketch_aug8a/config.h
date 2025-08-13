#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// -------------------- PINOUT / HARDWARE --------------------
#define DHTPIN         15
#define DHTTYPE        11          // DHT11
#define BUZZER         13
#define MQ2_PIN        34

// Dust sensor GP2Y1010:
#define G3_PIN         23          // LED IR control
#define G5_PIN         35          // Vo analog (ADC1)

// I2C pins for ESP32
#define I2C_SDA        21
#define I2C_SCL        22

// OLED SH1106
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64

// -------------------- WIFI + NTP --------------------
static String WIFI_SSID = "";
static String WIFI_PASS = "";
static const char* NTP_SERVERS[] = {
  "vn.pool.ntp.org",         // Việt Nam pool
  "asia.pool.ntp.org",       // Khu vực Châu Á
  "time.google.com",         // Google Public NTP
  "pool.ntp.org",            // Global pool
  "time.cloudflare.com",     // Cloudflare NTP
  "time.windows.com",        // Microsoft NTP
  "time.apple.com",          // Apple NTP
  "ntp.ubuntu.com",          // Ubuntu NTP
  "ntp1.ntp.ox.ac.uk",       // Oxford University (UK)
  "ntp1.ntu.edu.sg"          // NTU Singapore
};

static const long  GMT_OFFSET_SEC      = 7 * 3600;
static const int   DAYLIGHT_OFFSET_SEC = 0;

// -------------------- SAFE THRESHOLDS (nhạy hơn) --------------------
static const float TEMP_WARN  = 36.0;   // °C
static const float TEMP_DANG  = 38.0;   // °C
static const float HUM_LOW    = 35.0;   // %
static const float HUM_HIGH   = 80.0;   // %
static const int   MQ2_WARN   = 600;    // ADC
static const int   MQ2_DANG   = 1000;   // ADC
static const float   DUST_WARN  = 0.036;    // ADC
static const float   DUST_DANG  = 0.056;   // ADC

// Hysteresis
static const float TEMP_HYS   = 0.8f;
static const float HUM_HYS    = 0.95f;
static const float GAS_HYS    = 0.9f;
static const float DUST_HYS   = 0.9f;

// EMA
static const float EMA_ALPHA  = 0.3f;

// Nhịp cập nhật
static const uint32_t UPDATE_MS = 1000;

// -------------------- Types --------------------
enum Level { LV_OK=0, LV_WARN=1, LV_DANG=2 };

struct EnvState {
  // giá trị hiển thị /status và OLED
  float temp;       // EMA hoặc NAN
  float hum;        // EMA hoặc NAN
  int   gas;        // EMA int
  float   dust;       // EMA float
  int   level;      // 0/1/2
};

#endif
