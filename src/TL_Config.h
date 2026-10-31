/**
 * @file TL_Config.h
 * @brief Central configuration for the ThingsLinker Arduino library
 *
 * Adjust these values to match your deployment. All other source files
 * pull their settings from here — this is the only file you need to edit.
 */

#ifndef TL_CONFIG_H
#define TL_CONFIG_H

// ─────────────────────────────────────────────────────────────────────────────
// Debug output
// ─────────────────────────────────────────────────────────────────────────────
// Compile-time default. To silence ALL library output at compile time (zero
// overhead, no flash usage), add before your #include:
//   #define TL_DEBUG_DEFAULT false
// At runtime, call iot.debug(false) to suppress output without recompiling.
#ifndef TL_DEBUG_DEFAULT
  #define TL_DEBUG_DEFAULT true
#endif

extern bool _tlDebugEnabled;  // Defined in ThingsLinker.cpp

#define TL_LOG(msg)  do { if (_tlDebugEnabled) Serial.println(msg);  } while(0)
#define TL_LOGF(...) do { if (_tlDebugEnabled) Serial.printf(__VA_ARGS__); } while(0)

// ─────────────────────────────────────────────────────────────────────────────
// MQTT broker
// ─────────────────────────────────────────────────────────────────────────────
// ThingsLinker production broker.
// Port 8883 = TCP + TLS (used by ESP32 / Arduino devices).
// Port 8084 = WebSocket + TLS (used by browser clients — handled server-side).
// Both can be overridden with compiler flags (for a self-hosted or test broker).
#ifndef MQTT_SERVER
  #define MQTT_SERVER "mqtt.thingslinker.com"
#endif
#ifndef MQTT_PORT
  #define MQTT_PORT   8883
#endif

// ─────────────────────────────────────────────────────────────────────────────
// MQTT tuning
// ─────────────────────────────────────────────────────────────────────────────
// Buffer must fit the CONNECT packet including LWT topic + message.
// Typical ThingsLinker LWT topic is ~130 bytes; 512 is a comfortable margin.
#define MQTT_MAX_PACKET_SIZE 512
#define MQTT_KEEPALIVE       60    // seconds

// Buffer for one outgoing {"v": ..., "t": ...} payload. Topic + payload must fit
// in MQTT_MAX_PACKET_SIZE, which leaves room for strings of roughly 300
// characters. Longer values are rejected and logged, never truncated.
#define TL_PAYLOAD_BUFFER_SIZE 384

// ─────────────────────────────────────────────────────────────────────────────
// Offline storage (see TL_Offline.h)
// ─────────────────────────────────────────────────────────────────────────────
// How many readings are kept while offline unless the sketch asks for another
// number: iot.offlineStorage(true, 500). A typical reading uses about 100
// bytes of RAM, so 200 readings need roughly 20 KB.
#ifndef TL_OFFLINE_DEFAULT_RECORDS
  #define TL_OFFLINE_DEFAULT_RECORDS 200
#endif
// Storing stops making the store bigger when free memory falls below this
// (bytes); the oldest readings are dropped instead, so the sketch keeps working.
#ifndef TL_OFFLINE_MIN_FREE_HEAP
  #define TL_OFFLINE_MIN_FREE_HEAP 20000
#endif
// Pause between stored readings when sending them after a reconnect (ms).
// Keeps a long backlog from flooding the server or starving the sketch.
#ifndef TL_OFFLINE_SEND_INTERVAL_MS
  #define TL_OFFLINE_SEND_INTERVAL_MS 50
#endif
// After reconnecting, how long to wait for the clock (NTP / network time)
// before sending readings that were taken while the clock was not set (ms).
#ifndef TL_OFFLINE_CLOCK_WAIT_MS
  #define TL_OFFLINE_CLOCK_WAIT_MS 15000
#endif

// How often a connected device publishes its retained ONLINE again (ms).
// A broker may delete retained messages after a while (ours does after about
// a day); repeating it keeps dashboards opened later showing the device online.
#ifndef TL_STATUS_REFRESH_MS
  #define TL_STATUS_REFRESH_MS (6UL * 60UL * 60UL * 1000UL)
#endif

// An out-of-date OFFLINE on the device's own status topic is answered with
// ONLINE, at most once in this time (ms). A burst of OFFLINEs gets one answer.
// Keep it well under the backend's wait (OFFLINE_CONFIRM_SECONDS, 5 s).
#ifndef TL_STATUS_ANSWER_GAP_MS
  #define TL_STATUS_ANSWER_GAP_MS 2000UL
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Device commands (Reset WiFi from the app or portal)
// ─────────────────────────────────────────────────────────────────────────────
// A command older than this (seconds, by the command's own "t") is ignored,
// so one held back by the network or the broker cannot act much later.
// Only checked once the device's clock is set.
#ifndef TL_COMMAND_MAX_AGE_S
  #define TL_COMMAND_MAX_AGE_S 120
#endif
// How long the reset button must be held by default (ms): iot.resetButton().
#ifndef TL_RESET_HOLD_MS
  #define TL_RESET_HOLD_MS 3000
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Subscriptions
// ─────────────────────────────────────────────────────────────────────────────
// Maximum number of widget pins the library will hold callbacks for.
// Typical device dashboards use 4-10 widgets; 20 covers advanced use cases.
// Increase if you need more (each entry uses ~56 bytes of RAM).
#define MAX_SUBSCRIPTIONS 20

// ─────────────────────────────────────────────────────────────────────────────
// GSM modem — PWRKEY
// ─────────────────────────────────────────────────────────────────────────────
// Pass this value as the pwrPin argument to ThingsLinkerGSM::begin() when your
// board does NOT have PWRKEY wired to an ESP32 GPIO (e.g. PWRKEY is tied to VCC
// and the modem powers on automatically when power is applied).
//
//   iot.begin(APN, TL_NO_PWRKEY);
//   iot.begin(APN, 4);             // GPIO 4 controls PWRKEY
//
#define TL_NO_PWRKEY -1

// ─────────────────────────────────────────────────────────────────────────────
// BLE provisioning
// ─────────────────────────────────────────────────────────────────────────────
#define BLE_SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_WIFI_CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_STATUS_CHAR_UUID   "cba1d466-344c-4be3-ab3f-189f80dd7518"
#define BLE_CONFIRM_CHAR_UUID  "8ec90774-f8a8-4f5c-8e5c-3f9a7d8c6b2a"

// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────
// How long to wait for WL_CONNECTED before giving up (milliseconds).
#define WIFI_CONNECT_TIMEOUT 10000
// Quick active scan to detect whether the saved SSID is in range.
// If not found, BLE provisioning starts immediately instead of waiting the
// full WIFI_CONNECT_TIMEOUT — dramatically speeds up first-boot experience.
#define WIFI_SCAN_TIMEOUT    3000

// ─────────────────────────────────────────────────────────────────────────────
// OTA / REST API server
// ─────────────────────────────────────────────────────────────────────────────
// Backend URL used for OTA check and status-update HTTP calls (no trailing /).
//
// ► Cloud / production (default — no change needed):
//     https://api.thingslinker.com
//
// ► Self-hosted / local development:
//   Option A — edit this file once (affects all sketches):
//     #define TL_API_SERVER "http://192.168.1.10:8000"   // your server LAN IP
//
//   Option B — override per-sketch, BEFORE #include <ThingsLinker.h>:
//     #define TL_API_SERVER "http://192.168.1.10:8000"
//     #include <ThingsLinker.h>
#ifndef TL_API_SERVER
  #define TL_API_SERVER "https://api.thingslinker.com"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// NTP
// ─────────────────────────────────────────────────────────────────────────────
// NTP servers used to obtain a real Unix timestamp for MQTT payloads.
// The library initiates NTP sync right after WiFi connects.
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.cloudflare.com"
// Unix epoch threshold for NTP validity check: 2020-01-01 00:00:00 UTC
#define NTP_VALID_EPOCH 1577836800UL

// OTA with iot.enableOTA(): besides the MQTT doorbell, check the API this often
// (plus up to 10 % random) in case a doorbell was missed. 12 hours.
#ifndef TL_OTA_SAFETY_CHECK_MS
  #define TL_OTA_SAFETY_CHECK_MS (12UL * 60UL * 60UL * 1000UL)
#endif

#endif // TL_CONFIG_H
