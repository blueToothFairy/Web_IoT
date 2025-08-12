#ifndef SENSORS_H
#define SENSORS_H

#include "config.h"
#include "levels.h"
#include <DHT.h>
#include <HTTPClient.h>

static DHT dht(DHTPIN, DHTTYPE);

// EMA buffers
static float emaTemp = NAN, emaHum = NAN;
static float emaGas  = 0,   emaDust = 0;

// Hysteresis memories
static Level prevTemp = LV_OK, prevHum = LV_OK, prevGas = LV_OK, prevDust = LV_OK;

// Module state (to publish ra ngoài)
static EnvState g_state = {NAN, NAN, 0, 0, 0};

// ---- Dust raw read (y nguyên) ----
inline float Dust_ReadDensity() {
  digitalWrite(G3_PIN, LOW);             // LED ON
  delayMicroseconds(280);
  int v = analogRead(G5_PIN);            // ADC1
  delayMicroseconds(40);
  digitalWrite(G3_PIN, HIGH);            // LED OFF
  delayMicroseconds(9680);               // chu kỳ ~10ms
  float vOut = v * 3.3 / 4095.0;
  return (0.17 * vOut - 0.1 < 0) ? 0 : (0.17 * vOut - 0.1);
}

inline void Sensors_Init() {
  dht.begin();
  pinMode(MQ2_PIN, INPUT);
  pinMode(G5_PIN, INPUT);
}

inline const EnvState& Sensors_GetState() {
  return g_state;
}

inline void Sensors_Update1Hz() {
  // ===== READ =====
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool dht_ok = !(isnan(t) || isnan(h));

  int mq2Raw  = analogRead(MQ2_PIN);
  float dustRaw = Dust_ReadDensity();

  // ===== EMA =====
  if (dht_ok) {
    emaTemp = isnan(emaTemp) ? t : EMA_ALPHA*t + (1-EMA_ALPHA)*emaTemp;
    emaHum  = isnan(emaHum)  ? h : EMA_ALPHA*h + (1-EMA_ALPHA)*emaHum;
  }
  emaGas  = (emaGas==0)  ? mq2Raw  : EMA_ALPHA*mq2Raw  + (1-EMA_ALPHA)*emaGas;
  emaDust = (emaDust==0) ? dustRaw : EMA_ALPHA*dustRaw + (1-EMA_ALPHA)*emaDust;

  // ===== CLASSIFY =====
  Level lvTemp = prevTemp, lvHumL = prevHum;
  if (dht_ok) {
    lvTemp = levelWithHysFloat(emaTemp, TEMP_WARN, TEMP_DANG, TEMP_HYS, prevTemp);

    // Độ ẩm: cảnh báo nếu < HUM_LOW hoặc > HUM_HIGH (giữ nguyên cách làm)
    float humSeverity = 0.0f;
    if (emaHum < HUM_LOW)  humSeverity = HUM_LOW - emaHum;
    if (emaHum > HUM_HIGH) humSeverity = emaHum - HUM_HIGH;
    float humWarnGap = 5.0f;
    float humDangGap = 10.0f;
    Level target = LV_OK;
    if (humSeverity >= humDangGap) target = LV_DANG;
    else if (humSeverity >= humWarnGap) target = LV_WARN;
    if (prevHum == LV_DANG && humSeverity < humDangGap*HUM_HYS) target = LV_WARN;
    if (prevHum == LV_WARN && humSeverity < humWarnGap*HUM_HYS) target = LV_OK;
    lvHumL = target;
  }

  Level lvGas  = levelWithHysInt((int)emaGas,  MQ2_WARN,  MQ2_DANG,  GAS_HYS,  prevGas);
  Level lvDust = levelWithHysFloat(emaDust, DUST_WARN, DUST_DANG, DUST_HYS, prevDust);

  Level lvAll = (Level)max((int)lvTemp, max((int)lvHumL, max((int)lvGas, (int)lvDust)));

  // ===== UPDATE EXPORT STATE =====
  g_state.temp  = dht_ok ? emaTemp : NAN;
  g_state.hum   = dht_ok ? emaHum  : NAN;
  g_state.gas   = (int)emaGas;
  g_state.dust  = emaDust;
  g_state.level = (int)lvAll;

  // Remember hysteresis
  prevTemp = lvTemp; prevHum = lvHumL; prevGas = lvGas; prevDust = lvDust;

  // ===== SERIAL JSON (giữ nguyên định dạng) =====
  extern String Now_String();   // khai báo trước, thực thi ở wifi_ntp_http.h
  String tstr = Now_String();
  String payload = "{\"time\":\"" + tstr + "\","
                   "\"temp\":" + (dht_ok ? String(emaTemp,1) : "null") + ","
                   "\"hum\":"  + (dht_ok ? String(emaHum,1)  : "null") + ","
                   "\"gas\":"  + String((int)emaGas) + ","
                   "\"dust\":" + String(emaDust,1) + ","
                   "\"level\":" + String((int)lvAll) + "}";
  Serial.println(payload);

  HTTPClient client;
  client.begin("http://192.168.51.69:3000/data");
  client.addHeader("Content-Type", "application/json");

  int httpCode = client.POST(payload);
  Serial.println("POST status: " + String(httpCode));
  client.end();
}

#endif
