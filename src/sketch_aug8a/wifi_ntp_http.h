#ifndef WIFI_NTP_HTTP_H
#define WIFI_NTP_HTTP_H

#include "config.h"
#include "sensors.h"
#include "buzzer.h"
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <EEPROM.h>

static WebServer server(80);
// ---------- NTP ----------
inline void NTP_Ensure() {
  for (size_t i = 0; i < 10; i++) {
    const char* svr = NTP_SERVERS[i];
    Serial.print("Dong bo NTP: ");
    Serial.println(svr);
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, svr);
    unsigned long t0 = millis();
    while (millis() - t0 < 10000) {
      struct tm ti;
      if (getLocalTime(&ti)) {
        Serial.println("🕒 NTP OK");
        return;
      }
      delay(500);
      Serial.print(".");
    }
    Serial.println("\n⚠️ Thu server khac...");
  }
  Serial.println("⛔ Khong dong bo duoc NTP.");
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
  if (lv == 0) return F("OK");
  if (lv == 1) return F("CANH BAO");
  return F("NGUY HIEM");
}

inline String JSON_Status() {
  const EnvState& st = Sensors_GetState();
  String tstr = Now_String();
  String js = "{\"time\":\"" + tstr + "\",";
  if (isnan(st.temp) || isnan(st.hum)) {
    js += "\"temp\":null,\"hum\":null,";
  } else {
    js += "\"temp\":" + String(st.temp, 1) + ",\"hum\":" + String(st.hum, 1) + ",";
  }
  js += "\"gas\":" + String(st.gas) + ",";
  js += "\"dust\":" + String(st.dust) + ",";
  js += "\"level\":" + String(st.level) + ",";
  js += "\"level_text\":\"";
  js += String(EnvLevelText(st.level));
  js += "\"}";
  return js;
}

inline void HTTP_handleOn() {
  tone(BUZZER, 1500);
  delay(200);
  noTone(BUZZER);
  server.send(200, "text/plain", "OK: buzzer on (beep)");
}
inline void HTTP_handleOff() {
  noTone(BUZZER);
  server.send(200, "text/plain", "OK: buzzer off");
}
inline void HTTP_handleTest() {
  tone(BUZZER, 2000);
  delay(150);
  noTone(BUZZER);
  server.send(200, "text/plain", "OK: test beep");
}
inline void HTTP_handleStatus() {
  server.send(200, "application/json", JSON_Status());
}

inline void HTTP_Begin() {
  server.on("/on", HTTP_handleOn);
  server.on("/off", HTTP_handleOff);
  server.on("/test", HTTP_handleTest);
  server.on("/status", HTTP_handleStatus);
  server.begin();
}

inline void HTTP_Loop() {
  server.handleClient();
}

// ------------------- WIFI ----------------------
inline void loadWifiConfig() {
  int ssid_len = EEPROM.read(0);
  if (ssid_len > 0 && ssid_len < 32) {
    for (int i = 0; i < ssid_len; i++) {
      WIFI_SSID += char(EEPROM.read(1 + i));
    }
  }

  int pass_len = EEPROM.read(50);
  if (pass_len >= 0 && pass_len < 64) {
    for (int i = 0; i < pass_len; i++) {
      WIFI_PASS += char(EEPROM.read(51 + i));
    }
  }

  Serial.println("WiFi da luu: " + WIFI_SSID);
}

// Lưu WiFi vào EEPROM
inline void saveWifiConfig(String ssid, String password) {
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

// Chế độ cấu hình
inline void startConfigMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SafeSense360", "12345678");

  Serial.println("");
  Serial.println("WiFi: SafeSense360");
  Serial.println("Pass: 12345678");
  Serial.print("Truy cap: ");
  Serial.println(WiFi.softAPIP());

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
    WIFI_SSID = ssid;
    WIFI_PASS = password;

    server.send(200, "text/html", "<h3>Restarting...</h3>");

    delay(3000);
    ESP.restart();
  });

  server.begin();
  Serial.println("Web server da khoi dong!");
  while (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
  }
  NTP_Ensure();
}

inline bool is_WIFI_Connected() {
  return WiFi.status() == WL_CONNECTED;
}

// Kết nối WiFi
inline void connectWifi() {
  WiFi.mode(WIFI_AP_STA);
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID.c_str());
  Serial.print("PASS: ");
  Serial.println(WIFI_PASS.c_str());
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());

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
  } else {
    Serial.println("");
    Serial.println("Ket noi that bai! Chuyen sang che do cau hinh");
    startConfigMode();
  }
}

inline void WIFI_Begin() {
  EEPROM.begin(200);
  loadWifiConfig();
  if (WIFI_SSID.length() > 0) {
    Serial.println("Dang ket noi WiFi: " + WIFI_SSID);
    connectWifi();
  } else {
    Serial.println("Chua co cau hinh WiFi, vao che do setup");
    startConfigMode();
  }
}

#endif