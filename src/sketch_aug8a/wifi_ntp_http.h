#ifndef WIFI_NTP_HTTP_H
#define WIFI_NTP_HTTP_H

#include "config.h"
#include "sensors.h"
#include "buzzer.h"
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

static WebServer server(80);

// ---------- Wi‑Fi ----------
inline void WIFI_Connect(unsigned long timeout_ms = 20000) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Ket noi Wi-Fi ");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Wi-Fi OK");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⛔ Khong ket noi duoc Wi-Fi. Kiem tra SSID/mk & 2.4GHz.");
    while(true) delay(1000);
  }
}

// ---------- NTP ----------
inline void NTP_Ensure() {
  for (size_t i=0;i<4;i++) {
    const char* svr = NTP_SERVERS[i];
    Serial.print("Dong bo NTP: "); Serial.println(svr);
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, svr);
    unsigned long t0 = millis();
    while (millis() - t0 < 10000) {
      struct tm ti;
      if (getLocalTime(&ti)) {
        Serial.println("🕒 NTP OK");
        return;
      }
      delay(500); Serial.print(".");
    }
    Serial.println("\n⚠️ Thu server khac...");
  }
  Serial.println("⛔ Khong dong bo duoc NTP.");
  while(true) delay(1000);
}

inline String Now_String() {
  struct tm ti;
  if (getLocalTime(&ti)) {
    char buf[20];
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return String(buf);
  }
  return "NTP-syncing";
}

// ---------- HTTP handlers ----------
inline const __FlashStringHelper* EnvLevelText(int lv) {
  if (lv==0) return F("OK");
  if (lv==1) return F("CANH BAO");
  return F("NGUY HIEM");
}

inline String JSON_Status() {
  const EnvState& st = Sensors_GetState();
  String tstr = Now_String();
  String js = "{\"time\":\"" + tstr + "\",";
  if (isnan(st.temp) || isnan(st.hum)) {
    js += "\"temp\":null,\"hum\":null,";
  } else {
    js += "\"temp\":" + String(st.temp,1) + ",\"hum\":" + String(st.hum,1) + ",";
  }
  js += "\"gas\":" + String(st.gas) + ",";
  js += "\"dust\":" + String(st.dust) + ",";
  js += "\"level\":" + String(st.level) + ",";
  js += "\"level_text\":\"";
  js += String(EnvLevelText(st.level));
  js += "\"}";
  return js;
}

inline void HTTP_handleOn()  { tone(BUZZER, 1500); delay(200); noTone(BUZZER); server.send(200, "text/plain", "OK: buzzer on (beep)"); }
inline void HTTP_handleOff() { noTone(BUZZER); server.send(200, "text/plain", "OK: buzzer off"); }
inline void HTTP_handleTest(){ tone(BUZZER, 2000); delay(150); noTone(BUZZER); server.send(200, "text/plain", "OK: test beep"); }
inline void HTTP_handleStatus(){ server.send(200, "application/json", JSON_Status()); }

inline void HTTP_Begin() {
  server.on("/on",     HTTP_handleOn);
  server.on("/off",    HTTP_handleOff);
  server.on("/test",   HTTP_handleTest);
  server.on("/status", HTTP_handleStatus);
  server.begin();
  Serial.println(String(F("🌐 HTTP server: http://")) + WiFi.localIP().toString());
}

inline void HTTP_Loop() {
  server.handleClient();
}

#endif
