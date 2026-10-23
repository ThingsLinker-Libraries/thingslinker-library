/**
 * @file 14_GSM_GPS_Tracker.ino
 * @brief ThingsLinker GSM — GPS vehicle / asset tracker
 *
 * Uses the A7672 built-in GPS — no external GPS module needed.
 * Everything is handled by the library. Just call iot.startGPS() and
 * read coordinates with iot.gpsLat() / iot.gpsLng().
 *
 * ── Hardware ──────────────────────────────────────────────────────────────────
 *   Board  : ESP32 (WROOM-32 / DevKit)
 *   Modem  : Simcom A7672SA / A7672E / A7672S (has built-in GPS)
 *
 * ── Wiring ────────────────────────────────────────────────────────────────────
 *   ESP32 GPIO16 (RX2) ────── A7672 TX
 *   ESP32 GPIO17 (TX2) ────── A7672 RX
 *   ESP32 GPIO4        ────── A7672 PWRKEY
 *   External 4 V / 500 mA ── A7672 VCC
 *   Active GPS antenna  ───── A7672 ANT_GPS connector
 *
 * ── Dashboard Widgets ─────────────────────────────────────────────────────────
 *   V1 → Map widget         (live position)
 *   V2 → Gauge widget       (speed in km/h)
 *   V3 → Value Display      (altitude in metres)
 *   V4 → LED widget         (1 = GPS locked, 0 = searching)
 */

// ─────────────────────────────────────────────────────────────────────────────
// Credentials — replace with your values from the Organisation Portal
// ─────────────────────────────────────────────────────────────────────────────
#define AUTH_TOKEN    "your_device_auth_token"
#define BLUEPRINT_KEY "your_blueprint_key"

#define APN           "airtelgprs.com"   // your SIM card APN
#define APN_USER      ""
#define APN_PASS      ""

#define MODEM_RX_PIN  16
#define MODEM_TX_PIN  17

// PWRKEY — set to TL_NO_PWRKEY if your board powers the modem automatically
#define MODEM_PWR_PIN  4   // or: TL_NO_PWRKEY

#include <ThingsLinkerGSM.h>

ThingsLinkerGSM iot(Serial2, AUTH_TOKEN, BLUEPRINT_KEY);

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial2.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // Connect to ThingsLinker via 4G
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, put the username and password first:
  //   iot.begin("MQTT_USERNAME", "MQTT_PASSWORD", APN, MODEM_PWR_PIN, APN_USER, APN_PASS);
  iot.begin(APN, MODEM_PWR_PIN, APN_USER, APN_PASS);

  // Start the built-in GPS engine
  iot.startGPS();
}

void loop() {
  iot.run();   // handles GPS polling, MQTT, and auto-reconnect

  static unsigned long lastPublish = 0;

  if (millis() - lastPublish >= 5000UL) {
    lastPublish = millis();

    if (iot.gpsValid()) {
      // GPS fix acquired — publish to dashboard
      iot.map    ("V1", iot.gpsLat(), iot.gpsLng());
      iot.gauge  ("V2", iot.gpsSpeed());
      iot.display("V3", iot.gpsAltitude());
      iot.led    ("V4", true);

      Serial.printf("[GPS] lat=%.6f  lng=%.6f  speed=%.1f km/h  alt=%.0f m\n",
                    iot.gpsLat(), iot.gpsLng(),
                    iot.gpsSpeed(), iot.gpsAltitude());
    } else {
      // No fix yet — show searching state on dashboard
      iot.led("V4", false);
      Serial.println("[GPS] Searching for fix...");
    }
  }
}
