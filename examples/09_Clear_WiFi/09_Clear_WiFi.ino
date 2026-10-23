/**
 * ================================================
 *   ThingsLinker — Clear WiFi & Re-Provision
 * ================================================
 *
 * Use this sketch to:
 *   - Erase saved WiFi credentials
 *   - Restart BLE provisioning (change networks)
 *   - Factory-reset the device WiFi configuration
 *
 * Steps:
 *   1. Fill in your credentials below
 *   2. Upload the sketch
 *   3. Open Serial Monitor (115200 baud)
 *   4. When "BLE PROVISIONING ACTIVE" appears,
 *      open the ThingsLinker app to provision WiFi
 *
 * After successful provisioning:
 *   - Device saves the new WiFi credentials to flash
 *   - App confirms onboarding → device restarts
 *   - On next boot the device auto-connects (no BLE)
 *
 * To re-provision again: upload this sketch again
 * or call iot.resetWiFi() in your own code.
 * ================================================
 */

#include <ThingsLinker.h>

// ── Your device credentials ──────────────────────
ThingsLinker iot("YOUR_DEVICE_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");
// ────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  // Initialize first so MQTT credentials are stored before BLE starts.
  // resetWiFi() is called inside begin() when no credentials are found,
  // but calling it explicitly here forces a clean re-provisioning regardless
  // of whether credentials were previously saved.
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Clear any existing WiFi credentials and restart BLE.
  // (If begin() already started BLE due to missing credentials,
  //  resetWiFi() clears them and ensures a fresh BLE session.)
  iot.resetWiFi();
}

void loop() {
  // Keep running so BLE provisioning can complete.
  iot.run();

  // After provisioning, optionally publish test data.
  static unsigned long lastSend = 0;
  if (iot.wifiConnected() && iot.mqttConnected() && millis() - lastSend >= 5000) {
    lastSend = millis();

    float temperature = 22.0f + random(0, 80) / 10.0f;
    iot.gauge("V0", temperature);

    Serial.printf("[Test] Published temperature: %.1f °C\n", temperature);
  }
}

/*
 * ================================================
 * Expected Serial output (first run):
 * ================================================
 *
 *   [Reset] Clearing WiFi credentials...
 *   [Reset] Starting BLE provisioning...
 *   [BLE] Starting... Device: ThingsLinker_BC6575C55494
 *   [BLE] ✓ Advertising as: ThingsLinker_BC6575C55494
 *
 *   ========================================
 *     BLE PROVISIONING ACTIVE
 *   ========================================
 *   1. Open ThingsLinker app
 *   2. Tap "Add Device" → scan for BLE
 *   3. Select: ThingsLinker_BC6575C55494
 *   4. Enter WiFi credentials
 *   ========================================
 *
 *   [BLE] App connected
 *   [BLE] Received: {"ssid":"MyNetwork","password":"..."}
 *   [BLE] WiFi credentials received for SSID: MyNetwork
 *   [WiFi] Connecting to: MyNetwork
 *   [WiFi] ✓ Connected! IP: 192.168.1.42  RSSI: -55 dBm
 *   [WiFi] NTP sync started (pool.ntp.org)
 *   [BLE] Status sent: {"status":"connected","ip":"192.168.1.42"}
 *   [BLE] ✓ WiFi OK — waiting for app to confirm onboarding...
 *   [BLE] Confirmation: {"status":"complete"}
 *   [BLE] ✓ Onboarding confirmed — restarting...
 *
 * ================================================
 */
