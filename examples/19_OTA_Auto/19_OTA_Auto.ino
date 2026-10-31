/**
 * ThingsLinker — Example 19: Automatic OTA updates (MQTT doorbell)
 *
 * The device does not poll the server for updates. When an OTA shipment
 * starts, the server rings a retained MQTT "doorbell" for each of its devices;
 * the library then asks the API over HTTPS what to install, downloads it,
 * reports the result and restarts into the new firmware. A device that is
 * offline hears the doorbell when it reconnects.
 *
 * As a safety net it also checks every 12 hours (TL_OTA_SAFETY_CHECK_MS, plus
 * a random spread) and once a few minutes after start-up.
 *
 * A failed update is retried by the server (Shipment → Attempts limit), and
 * devices that used up their attempts get new ones after the shipment's
 * "Attempts reset period".
 *
 * Partition scheme — the SAME for this sketch and every .bin you ship:
 *   Tools → Flash Size: your chip's size (e.g. 16MB for an ESP32-S3 N16R8)
 *   Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)
 */

#include <ThingsLinker.h>

// ── Device credentials (Org Portal → Blueprints → [Blueprint] → Devices → [Device]) ──
#define AUTH_TOKEN    "YOUR_DEVICE_AUTH_TOKEN"   // ~64 hex characters
#define BLUEPRINT_ID  "YOUR_BLUEPRINT_ID"        // starts with "BLUE"

// Shown at start-up so you can see which firmware is running after an update
static const char* FIRMWARE_VERSION = "1.0";

ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\nThingsLinker automatic OTA  v%s\n", FIRMWARE_VERSION);

  iot.begin();       // WiFi (or BLE setup) + MQTT
  iot.enableOTA();   // doorbell + safety-net checks, handled inside iot.run()
}

void loop() {
  iot.run();         // also installs an update when the doorbell rings

  // ── Your application code goes here ──────────────────────────────────────
}
