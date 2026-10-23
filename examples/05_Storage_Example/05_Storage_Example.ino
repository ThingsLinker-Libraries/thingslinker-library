/**
 * ========================================
 *   ThingsLinker - Storage Example
 * ========================================
 *
 * This example shows how to save and retrieve data
 * sent from the mobile app using ESP32 Preferences.
 *
 * Features:
 * - Receive device name from app and save to flash
 * - Receive temperature threshold and save
 * - Receive alarm status and save
 * - Data persists after reboot
 * - Send stored values back to app
 */

#include <ThingsLinker.h>

// Your credentials (blueprintId is mandatory)
ThingsLinker iot("YOUR_DEVICE_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");

// LED pin
const int LED_PIN = 2;

void setup() {
  pinMode(LED_PIN, OUTPUT);

  // Initialize ThingsLinker with MQTT credentials
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Load saved data on startup
  String deviceName = iot.getString("deviceName", "Unnamed Device");
  int threshold = iot.getInt("threshold", 30);
  bool alarmEnabled = iot.getBool("alarmEnabled", false);

  Serial.println("\n========== Stored Data ==========");
  Serial.println("Device Name: " + deviceName);
  Serial.println("Threshold: " + String(threshold) + "°C");
  Serial.println("Alarm: " + String(alarmEnabled ? "Enabled" : "Disabled"));
  Serial.println("================================\n");

  // V0: Receive device name from app (text input widget)
  iot.onValue("V0", [](float value) {
    // For text, we'll use a number code to represent letters
    // In a real app, you'd send actual strings via MQTT payload
    // For this example, we'll just save whatever number is sent
    String deviceName = "Device_" + String((int)value);
    iot.saveString("deviceName", deviceName);
    Serial.println("✓ Saved device name: " + deviceName);
  });

  // V1: Receive temperature threshold from app (slider widget)
  iot.onSlider("V1", [](float value) {
    int threshold = (int)value;
    iot.saveInt("threshold", threshold);
    Serial.println("✓ Saved threshold: " + String(threshold) + "°C");

    // Send confirmation back to app
    iot.gauge("V4", threshold);
  });

  // V2: Receive alarm enable/disable from app (button widget)
  iot.onButton("V2", [](bool enabled) {
    iot.saveBool("alarmEnabled", enabled);
    Serial.println("✓ Saved alarm: " + String(enabled ? "Enabled" : "Disabled"));

    // Update LED
    digitalWrite(LED_PIN, enabled ? HIGH : LOW);

    // Send confirmation back to app
    iot.led("V5", enabled);
  });

  // V3: Reset all stored data (button widget)
  iot.onButton("V3", [](bool pressed) {
    if (pressed) {
      iot.clearAllData();
      Serial.println("✓ Cleared all stored data!");

      // Reset to defaults
      digitalWrite(LED_PIN, LOW);

      // Send defaults to app
      iot.gauge("V4", 30);
      iot.led("V5", false);
    }
  });

  // Send current values to app on startup
  iot.gauge("V4", threshold);
  iot.led("V5", alarmEnabled);

  // Set LED based on saved alarm status
  digitalWrite(LED_PIN, alarmEnabled ? HIGH : LOW);
}

void loop() {
  // Run ThingsLinker
  iot.run();

  // Simulate temperature reading and check threshold
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 5000) {
    lastRead = millis();

    // Read stored threshold
    int threshold = iot.getInt("threshold", 30);
    bool alarmEnabled = iot.getBool("alarmEnabled", false);

    // Simulate temperature
    float temperature = random(200, 400) / 10.0;  // 20.0 - 40.0

    // Send temperature to app (gauge on V6)
    iot.gauge("V6", temperature);

    // Check if temperature exceeds threshold
    if (alarmEnabled && temperature > threshold) {
      Serial.println("⚠️  ALARM! Temperature " + String(temperature) + "°C exceeds threshold " + String(threshold) + "°C");

      // Blink LED
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));

      // Send alert to app (button on V7)
      iot.button("V7", true);
    } else {
      // Normal - turn off alert
      iot.button("V7", false);
    }

    // Print status
    Serial.print("Temp: " + String(temperature) + "°C | ");
    Serial.print("Threshold: " + String(threshold) + "°C | ");
    Serial.println("Alarm: " + String(alarmEnabled ? "ON" : "OFF"));
  }
}

/*
 * ========================================
 * How to use:
 * ========================================
 *
 * 1. Upload this code to ESP32
 * 2. Connect device via BLE (first time only)
 * 3. In ThingsLinker app, add these widgets:
 *
 *    Pin V0: VALUE INPUT (for device name - send numbers)
 *    Pin V1: SLIDER (range 0-50 for temperature threshold)
 *    Pin V2: BUTTON (enable/disable alarm)
 *    Pin V3: BUTTON (reset all data)
 *    Pin V4: GAUGE (shows current threshold, 0-50°C)
 *    Pin V5: LED (shows alarm status)
 *    Pin V6: GAUGE (real-time temperature, 0-50°C)
 *    Pin V7: LED (alert indicator - red when exceeded)
 *
 * 4. Set threshold using slider on V1
 * 5. Enable alarm using button on V2
 * 6. Watch LED blink when temperature exceeds threshold!
 * 7. Restart ESP32 - all settings are saved!
 *
 * ========================================
 * Storage Functions Available:
 * ========================================
 *
 * // Save data
 * iot.saveString("key", "value");     // Save string
 * iot.saveInt("key", 123);            // Save integer
 * iot.saveFloat("key", 25.5);         // Save float
 * iot.saveBool("key", true);          // Save boolean
 *
 * // Get data
 * String s = iot.getString("key", "default");
 * int i = iot.getInt("key", 0);
 * float f = iot.getFloat("key", 0.0);
 * bool b = iot.getBool("key", false);
 *
 * // Check and remove
 * if (iot.hasKey("key")) {
 *   iot.removeKey("key");
 * }
 *
 * // Clear all app data (WiFi credentials are safe!)
 * iot.clearAllData();
 *
 * ========================================
 * Data Persistence:
 * ========================================
 *
 * - Data is stored in ESP32 flash memory
 * - Survives power loss and reboots
 * - Maximum 4000 characters per string
 * - Maximum 15 characters for key name
 * - WiFi credentials stored separately (not affected by clearAllData)
 *
 * ========================================
 */
