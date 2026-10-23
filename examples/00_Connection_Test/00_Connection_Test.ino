/**
 * ================================================
 *   ThingsLinker — Connection Test
 * ================================================
 *
 * Use this sketch to verify your device credentials
 * and confirm end-to-end connectivity to the
 * ThingsLinker platform before building your project.
 *
 * What this sketch does:
 *   1. Provisions WiFi via BLE (first boot only)
 *   2. Connects to mqtt.thingslinker.com:8883 (TLS)
 *   3. Publishes a simulated temperature reading to V0
 *      every 5 seconds
 *   4. Prints connection status to Serial
 *
 * How to get your credentials:
 *   1. Log in to the ThingsLinker organisation portal
 *   2. Open Blueprints → [your blueprint] → Devices → [your device]
 *   3. Copy:
 *        - Device Auth Token
 *        - Blueprint ID  (e.g. "BLUExxxxxxxxxxxxxxxx")
 *
 * Expected Serial output (115200 baud):
 *   ========================================
 *      ThingsLinker IoT Library v2.0
 *   ========================================
 *   Chip ID : XXXXXXXXXXXX
 *   Broker  : mqtt.thingslinker.com:8883
 *   ========================================
 *
 *   [Setup] Saved WiFi found, connecting...
 *   [WiFi] ✓ Connected! IP: 192.168.1.42  RSSI: -58 dBm
 *   [WiFi] NTP sync started (pool.ntp.org)
 *   [MQTT] Connecting to mqtt.thingslinker.com:8883
 *   [MQTT] ✓ Connected!
 *   [MQTT] ✓ Status → ONLINE
 *   [Setup] ✓ MQTT connected — device ready!
 *   [MQTT] ✓ V0 = 24.50
 *   [MQTT] ✓ V0 = 25.13
 *   ...
 *
 * ================================================
 */

#include <ThingsLinker.h>

// ── Paste your credentials here ─────────────────
ThingsLinker iot("YOUR_DEVICE_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");
// ────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();
}

void loop() {
  iot.run();

  // Publish a simulated temperature reading every 5 seconds
  static unsigned long lastSend = 0;
  if (iot.wifiConnected() && iot.mqttConnected() && millis() - lastSend >= 5000) {
    lastSend = millis();

    float temperature = 22.0f + random(0, 80) / 10.0f;   // 22.0 – 30.0 °C
    iot.gauge("V0", temperature);

    Serial.printf("[Test] Published temperature: %.1f °C\n", temperature);
  }
}
