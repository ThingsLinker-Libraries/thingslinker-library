/**
 * @file TL_WiFi.cpp
 * @brief WiFi management — connect, persist credentials, reconnect
 */

#include "TL_WiFi.h"
#include "TL_Config.h"

#ifdef ESP32
  #include <WiFi.h>
  #include <Preferences.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#ifdef ESP32
  static Preferences _prefs;
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/** Block until WL_CONNECTED or timeout. Returns true if connected. */
static bool waitForConnect() {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_CONNECT_TIMEOUT) return false;
    delay(250);
    if (_tlDebugEnabled) Serial.print(".");
  }
  if (_tlDebugEnabled) Serial.println();
  return true;
}

/** Initiate NTP sync. Call after WiFi connects. Non-blocking — sync happens in background. */
static void startNTP() {
#ifdef ESP32
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  TL_LOG("[WiFi] NTP sync started (" NTP_SERVER_1 ")");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool connectWiFi(const char* ssid, const char* password, bool saveCredentials) {
  TL_LOG("[WiFi] Connecting to: " + String(ssid));

  // The library keeps the network in its own storage. Without this the WiFi
  // driver would keep a second copy of the password in flash.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  if (!waitForConnect()) {
    TL_LOG("[WiFi] ✗ Connection failed (timeout)");
    return false;
  }

  TL_LOG("[WiFi] ✓ Connected! IP: " + WiFi.localIP().toString() +
         "  RSSI: " + String(WiFi.RSSI()) + " dBm");

  if (saveCredentials) {
#ifdef ESP32
    _prefs.begin("thingslinker", false);
    _prefs.putString("ssid",     ssid);
    _prefs.putString("password", password);
    _prefs.putBool("saved", true);
    _prefs.end();
    TL_LOG("[WiFi] ✓ Credentials saved to flash");
#endif
  }

  startNTP();
  return true;
}

bool connectWiFi() {
#ifdef ESP32
  _prefs.begin("thingslinker", true);
  bool saved = _prefs.getBool("saved", false);
  String ssid     = _prefs.getString("ssid",     "");
  String password = _prefs.getString("password", "");
  _prefs.end();

  if (!saved || ssid.length() == 0) {
    TL_LOG("[WiFi] No saved credentials");
    return false;
  }

  return connectWiFi(ssid.c_str(), password.c_str(), false);
#else
  TL_LOG("[WiFi] No saved credentials");
  return false;
#endif
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  TL_LOG("[WiFi] Disconnected");
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void clearWiFiCredentials() {
#ifdef ESP32
  _prefs.begin("thingslinker", false);
  _prefs.clear();
  _prefs.end();
  // Also wipe the WiFi driver's own copy, which firmware built with an older
  // version of the library may have saved. Needs the WiFi station running.
  if (WiFi.getMode() & WIFI_STA) WiFi.disconnect(false, true);
  TL_LOG("[WiFi] ✓ Credentials cleared");
#endif
}

bool hasWiFiCredentials() {
#ifdef ESP32
  _prefs.begin("thingslinker", true);
  bool saved = _prefs.getBool("saved", false);
  _prefs.end();
  return saved;
#else
  return false;
#endif
}

bool isSavedNetworkVisible() {
#ifdef ESP32
  _prefs.begin("thingslinker", true);
  String savedSsid = _prefs.getString("ssid", "");
  _prefs.end();

  if (savedSsid.length() == 0) return false;

  TL_LOG("[WiFi] Scanning for: " + savedSsid);

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true, false, 200);  // quick active scan

  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == savedSsid) { found = true; break; }
  }
  WiFi.scanDelete();

  TL_LOG(found ? "[WiFi] Network found in range" : "[WiFi] Network not in range");
  return found;
#else
  return false;
#endif
}

String getWiFiIP() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "0.0.0.0";
}

int getWiFiRSSI() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
}
