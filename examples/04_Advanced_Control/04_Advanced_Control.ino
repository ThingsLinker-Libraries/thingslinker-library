/**
 * ========================================
 *   ThingsLinker - Advanced Control
 * ========================================
 *
 * Advanced example showing multiple widgets:
 * - Button to control LED
 * - Slider to control LED brightness
 * - Send temperature to gauge
 * - Status functions
 */

#include <ThingsLinker.h>

// Your credentials (blueprintId is mandatory)
ThingsLinker iot("YOUR_DEVICE_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");

// LED pin
const int LED_PIN = 2;

void setup() {
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);

  // Initialize ThingsLinker with MQTT credentials
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Button on V0 - ON/OFF control
  iot.onButton("V0", [](bool value) {
    digitalWrite(LED_PIN, value ? HIGH : LOW);
    Serial.println(value ? "Button: LED ON" : "Button: LED OFF");
  });

  // Slider on V1 - Brightness control (0-255)
  iot.onSlider("V1", [](float value) {
    analogWrite(LED_PIN, (int)value);
    Serial.print("Slider: Brightness = ");
    Serial.println((int)value);
  });
}

void loop() {
  // Run ThingsLinker
  iot.run();

  // Send sensor data every 5 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 5000) {
    lastSend = millis();

    // Send temperature to gauge on V2
    float temperature = random(200, 350) / 10.0;
    iot.gauge("V2", temperature);

    // Send humidity to gauge on V3
    float humidity = random(300, 800) / 10.0;
    iot.gauge("V3", humidity);

    // Print status
    Serial.println("\n--- Device Status ---");
    Serial.print("WiFi: ");
    Serial.println(iot.wifiConnected() ? "Connected" : "Disconnected");
    Serial.print("MQTT: ");
    Serial.println(iot.mqttConnected() ? "Connected" : "Disconnected");
    Serial.print("IP: ");
    Serial.println(iot.getIP());
    Serial.print("Chip ID: ");
    Serial.println(iot.getChipID());
    Serial.println("--------------------\n");
  }
}

/*
 * ========================================
 * How to use:
 * ========================================
 *
 * 1. Upload this code to ESP32
 * 2. Connect device via BLE
 * 3. In ThingsLinker app, add these widgets:
 *
 *    Pin V0: BUTTON widget (LED on/off)
 *    Pin V1: SLIDER widget (range 0-255 for brightness)
 *    Pin V2: GAUGE widget (temperature, 0-50°C)
 *    Pin V3: GAUGE widget (humidity, 0-100%)
 *
 * 4. Control and monitor your device!
 *
 * ========================================
 * Status Functions:
 * ========================================
 *
 * iot.wifiConnected()  - Check WiFi status
 * iot.mqttConnected()  - Check MQTT status
 * iot.bleActive()      - Check if BLE is active
 * iot.getIP()          - Get device IP address
 * iot.getChipID()      - Get unique chip ID
 *
 * ========================================
 * Reset WiFi:
 * ========================================
 *
 * To reconfigure WiFi, add a button that calls:
 * iot.resetWiFi();
 *
 * This will clear saved WiFi and restart BLE provisioning.
 *
 * ========================================
 */
