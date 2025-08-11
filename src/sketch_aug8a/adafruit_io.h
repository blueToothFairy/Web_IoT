#ifndef ADAFRUIT_IO_H
#define ADAFRUIT_IO_H

#include <Arduino.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// Nếu có module tách sẵn
#ifdef __has_include
  #if __has_include("config.h")
    #include "config.h"
  #endif
  #if __has_include("sensors.h")
    #include "sensors.h"
  #endif
#endif

// ===== CONFIG USER (sửa theo tài khoản của bạn) =====
#ifndef AIO_USERNAME
  #define AIO_USERNAME  "luuquang2005"
#endif

#ifndef AIO_KEY
  #define AIO_KEY       "aio_Udfe24Z4Jur2XTDkGTy2QZRMBZ82"
#endif

#ifndef AIO_SERVER
  #define AIO_SERVER    "io.adafruit.com"
#endif

#ifndef AIO_SERVERPORT
  #define AIO_SERVERPORT  1883    // 1883 = MQTT, 8883 = MQTT TLS
#endif

#ifndef AIO_PERIOD_MS
  #define AIO_PERIOD_MS   15000UL  // gửi mỗi 60 giây
#endif

// Feed names
#ifndef AIO_FEED_TEMP
  #define AIO_FEED_TEMP  AIO_USERNAME "/feeds/nhiet-do"
#endif
#ifndef AIO_FEED_HUM
  #define AIO_FEED_HUM   AIO_USERNAME "/feeds/do-am"
#endif
#ifndef AIO_FEED_GAS
  #define AIO_FEED_GAS   AIO_USERNAME "/feeds/gas"
#endif
#ifndef AIO_FEED_DUST
  #define AIO_FEED_DUST  AIO_USERNAME "/feeds/bui"
#endif

// ====== Internal objects ======
static WiFiClient              _aio_client;
static Adafruit_MQTT_Client    _aio_mqtt(&_aio_client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Publishers
static Adafruit_MQTT_Publish   _aio_pubTemp(&_aio_mqtt, AIO_FEED_TEMP);
static Adafruit_MQTT_Publish   _aio_pubHum (&_aio_mqtt, AIO_FEED_HUM);
static Adafruit_MQTT_Publish   _aio_pubGas (&_aio_mqtt, AIO_FEED_GAS);
static Adafruit_MQTT_Publish   _aio_pubDust(&_aio_mqtt, AIO_FEED_DUST);

// Publish timer
static unsigned long           _aio_lastPubMs = 0;

// ====== MQTT connect helper ======
inline void _AIO_Connect() {
  if (_aio_mqtt.connected()) return;
  Serial.print("[AIO] Connecting MQTT ... ");
  int8_t ret;
  uint8_t retries = 3;
  while ((ret = _aio_mqtt.connect()) != 0) {
    Serial.print("fail: ");
    Serial.println(_aio_mqtt.connectErrorString(ret));
    _aio_mqtt.disconnect();
    if (!--retries) break;
    delay(2000);
    Serial.print("[AIO] retry ... ");
  }
  if (_aio_mqtt.connected()) Serial.println("OK");
  else Serial.println("GIVE UP (will retry later).");
}

// ====== Public API ======
inline void AIO_Begin() {
  _AIO_Connect();
  _aio_lastPubMs = millis();
  Serial.printf("[AIO] Ready. server=%s port=%d user=%s\n",
                AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME);
}

// Gửi dữ liệu cảm biến trực tiếp
inline bool AIO_PublishNow(
  float temperature, bool temp_valid,
  float humidity,    bool hum_valid,
  int   gas,
  int   dust
) {
  if (!_aio_mqtt.connected()) return false;
  bool ok = true;
  if (temp_valid) ok &= _aio_pubTemp.publish((double)temperature);
  if (hum_valid)  ok &= _aio_pubHum.publish((double)humidity);
  ok &= _aio_pubGas.publish((int32_t)gas);
  ok &= _aio_pubDust.publish((int32_t)dust);
  return ok;
}

// Nếu dùng module sensors.h
#if __has_include("sensors.h")
inline bool AIO_PublishFromSensors() {
  if (!_aio_mqtt.connected()) return false;
  const EnvState& st = Sensors_GetState();
  bool ok = true;
  if (!isnan(st.temp)) ok &= _aio_pubTemp.publish((double)st.temp);
  if (!isnan(st.hum))  ok &= _aio_pubHum.publish((double)st.hum);
  ok &= _aio_pubGas.publish((int32_t)st.gas);
  ok &= _aio_pubDust.publish((int32_t)st.dust);
  return ok;
}
#endif

// Gọi trong loop()
inline void AIO_Tick() {
  _AIO_Connect();
  if (_aio_mqtt.connected()) {
    _aio_mqtt.processPackets(10);
    _aio_mqtt.ping();
  }

  unsigned long now = millis();
  if (now - _aio_lastPubMs < AIO_PERIOD_MS) return;
  _aio_lastPubMs = now;

#if __has_include("sensors.h")
  bool ok = AIO_PublishFromSensors();
#else
  bool ok = true; // không có sensors.h thì tự gọi AIO_PublishNow bên ngoài
#endif

  if (ok) Serial.println("[AIO] Published.");
  else    Serial.println("[AIO] Publish failed (will retry).");
}

#endif // ADAFRUIT_IO_H
