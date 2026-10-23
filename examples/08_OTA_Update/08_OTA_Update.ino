/**
 * ThingsLinker — Example 08: OTA Firmware Update
 * ================================================
 *
 * Demonstrates automatic Over-The-Air firmware updates via the ThingsLinker
 * platform. The device connects to WiFi + MQTT, then periodically checks the
 * backend for a pending firmware shipment. If one is found it downloads and
 * flashes the new firmware, then restarts.
 *
 * Step-by-step setup
 * ------------------
 *  1. Fill in your credentials below (from Org Portal → Blueprints → Devices).
 *  2. Upload THIS sketch to your ESP32 (this becomes "v1.0.0").
 *  3. In the Org Portal:
 *       a. Go to OTA → New Shipping
 *       b. Upload a compiled .bin of example 11_OTA_TestFirmware (v2.0)
 *       c. Set shipment status → Live
 *  4. Open Serial Monitor (115200 baud) — within 60 s the device will
 *     detect the update, flash it, and restart with the new firmware.
 *
 * IMPORTANT — Partition Scheme & Bootloader
 * ------------------------------------------
 * OTA requires an OTA-capable partition scheme for BOTH this sketch and the
 * new .bin you upload via the portal. In Arduino IDE set:
 *   Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)
 *   ("Default with OTA" leaves 1.3MB per app, which a WiFi + OTA sketch now
 *   slightly exceeds.)
 *
 * Rules:
 *  - Use the SAME partition scheme for every firmware on this device.
 *  - The bootloader is flashed once via USB and never changed by OTA.
 *  - Compiling the new .bin with a different scheme (e.g. "No OTA" or "Huge APP")
 *    will cause the device to crash after the OTA flash — it looks like success
 *    but the app won't boot.
 *  - To change the partition scheme you MUST re-flash via USB.
 *
 * API server
 * ----------
 *  Cloud (default — no change needed): https://api.thingslinker.com
 *  Local dev: uncomment the line below and set your server's LAN IP:
 *    #define TL_API_SERVER "http://192.168.1.10:8000"
 *  This must appear BEFORE #include <ThingsLinker.h>.
 *  Alternatively, edit TL_API_SERVER in src/TL_Config.h once for all sketches.
 *
 * Expected Serial output
 * ----------------------
 *  [OTA] Checking for firmware update...
 *  [OTA] Pending update: v2.0
 *  [OTA] Flash successful — restarting in 1 s...
 *  (device restarts with new firmware)
 */

// ── Local dev only: uncomment and set your server's LAN IP ───────────────────
// #define TL_API_SERVER "http://192.168.1.10:8000"

#include <ThingsLinker.h>

// ── Device credentials (Org Portal → Blueprints → [Blueprint] → Devices → [Device]) ──
#define AUTH_TOKEN    "YOUR_DEVICE_AUTH_TOKEN"   // ~64 hex characters
#define BLUEPRINT_ID  "YOUR_BLUEPRINT_ID"        // starts with "BLUE"

// ── OTA check interval ────────────────────────────────────────────────────────
// How often the device polls the backend for pending updates.
// 60 s is a good default; reduce to 10 s during initial testing.
static const unsigned long OTA_INTERVAL_MS = 60000UL;

// ── Current firmware version (displayed in Serial output) ─────────────────────
// Bump this string when building the "new" firmware so you can confirm the
// device restarted with the updated build.
static const char* FIRMWARE_VERSION = "1.0";

// ── ThingsLinker instance ─────────────────────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

static unsigned long _lastOtaCheck = 0;

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.printf( "   ThingsLinker OTA Demo  v%s\n", FIRMWARE_VERSION);
  Serial.println("   API: " TL_API_SERVER);
  Serial.println("========================================");

  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Trigger an immediate OTA check after the first OTA_INTERVAL elapses.
  // Setting _lastOtaCheck = 0 ensures the first check happens as soon as
  // WiFi is up in loop().
  _lastOtaCheck = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  iot.run();   // Handles WiFi + MQTT + reconnect

  // ── Periodic OTA check ─────────────────────────────────────────────────────
  if (iot.wifiConnected() && (millis() - _lastOtaCheck >= OTA_INTERVAL_MS)) {
    _lastOtaCheck = millis();

    Serial.println("[OTA] Checking for firmware update...");
    OTAResult result = iot.checkOTA();   // Uses TL_API_SERVER defined above

    switch (result) {
      case OTA_NO_UPDATE:
        Serial.println("[OTA] Firmware is up to date.");
        break;

      case OTA_SUCCESS:
        // Device restarted inside checkOTA() — this line is never reached.
        break;

      case OTA_FAILED:
        Serial.println("[OTA] Update failed. Check Serial for details. Will retry.");
        break;

      case OTA_ERROR:
        Serial.println("[OTA] Could not reach the update server. Check API_SERVER / WiFi.");
        break;
    }
  }

  // ── Your application code goes here ────────────────────────────────────────
  // e.g. read sensors, publish data, handle callbacks
}
