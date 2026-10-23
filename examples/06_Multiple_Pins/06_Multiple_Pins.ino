/**
 * ========================================
 *   ThingsLinker - Multiple Pins Example
 * ========================================
 *
 * This example shows how to use many virtual pins!
 * ThingsLinker supports V0 to V124 (125 pins total)
 *
 * Perfect for complex projects with many sensors,
 * actuators, and controls!
 *
 * Features:
 * - Multiple buttons (V0-V4)
 * - Multiple sensors (V10-V14)
 * - Multiple LEDs (V20-V24)
 * - Demonstrates V0, V50, V100, V124 usage
 */

#include <ThingsLinker.h>

// Your credentials (blueprintId is mandatory)
ThingsLinker iot("YOUR_DEVICE_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");

// Pin definitions
const int LED_PINS[] = {2, 4, 5, 18, 19};  // 5 LEDs
const int SENSOR_PINS[] = {32, 33, 34, 35, 36};  // 5 analog sensors

void setup() {
  // Initialize LED pins
  for (int i = 0; i < 5; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Initialize ThingsLinker
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // ========== Button Controls (V0-V4) ==========
  // Control 5 different LEDs from app

  iot.onButton("V0", [](bool state) {
    digitalWrite(LED_PINS[0], state ? HIGH : LOW);
    Serial.println("LED 0: " + String(state ? "ON" : "OFF"));
  });

  iot.onButton("V1", [](bool state) {
    digitalWrite(LED_PINS[1], state ? HIGH : LOW);
    Serial.println("LED 1: " + String(state ? "ON" : "OFF"));
  });

  iot.onButton("V2", [](bool state) {
    digitalWrite(LED_PINS[2], state ? HIGH : LOW);
    Serial.println("LED 2: " + String(state ? "ON" : "OFF"));
  });

  iot.onButton("V3", [](bool state) {
    digitalWrite(LED_PINS[3], state ? HIGH : LOW);
    Serial.println("LED 3: " + String(state ? "ON" : "OFF"));
  });

  iot.onButton("V4", [](bool state) {
    digitalWrite(LED_PINS[4], state ? HIGH : LOW);
    Serial.println("LED 4: " + String(state ? "ON" : "OFF"));
  });

  // ========== Middle Range Pins (V50) ==========
  // Master control - turn all LEDs on/off

  iot.onButton("V50", [](bool state) {
    Serial.println("Master Control: " + String(state ? "ALL ON" : "ALL OFF"));
    char pin[8];
    for (int i = 0; i < 5; i++) {
      snprintf(pin, sizeof(pin), "V%d", 20 + i);
      digitalWrite(LED_PINS[i], state ? HIGH : LOW);
      iot.led(pin, state);  // Update LED widgets
    }
  });

  // ========== Slider Controls (V60-V64) ==========
  // Control LED brightness with PWM

  iot.onSlider("V60", [](float value) {
    analogWrite(LED_PINS[0], (int)value);
    Serial.println("LED 0 Brightness: " + String((int)value));
  });

  iot.onSlider("V61", [](float value) {
    analogWrite(LED_PINS[1], (int)value);
    Serial.println("LED 1 Brightness: " + String((int)value));
  });

  // ========== High Range Pins (V100+) ==========
  // Configuration and settings

  iot.onValue("V100", [](float value) {
    // Update interval in seconds
    Serial.println("Update interval set to: " + String((int)value) + " seconds");
    iot.saveInt("interval", (int)value);  // Save to flash
  });

  iot.onValue("V101", [](float value) {
    // Threshold setting
    Serial.println("Threshold set to: " + String(value));
    iot.saveFloat("threshold", value);  // Save to flash
  });

  // ========== Last Pin (V124) ==========
  // Factory reset button

  iot.onButton("V124", [](bool pressed) {
    if (pressed) {
      Serial.println("🔄 Factory Reset Triggered!");

      // Turn off all LEDs
      char pin[8];
      for (int i = 0; i < 5; i++) {
        snprintf(pin, sizeof(pin), "V%d", 20 + i);
        digitalWrite(LED_PINS[i], LOW);
        iot.led(pin, false);
      }

      // Clear all stored data
      iot.clearAllData();

      // Send confirmation
      iot.button("V124", false);
      Serial.println("✓ Reset complete!");
    }
  });

  Serial.println("\n========================================");
  Serial.println("   Multiple Pins Example Ready!");
  Serial.println("   Pins Used: V0-V4, V10-V14, V20-V24,");
  Serial.println("   V50, V60-V61, V100-V101, V124");
  Serial.println("   Total: 125 pins available (V0-V124)");
  Serial.println("========================================\n");
}

void loop() {
  // Run ThingsLinker
  iot.run();

  // Send sensor data every 2 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 2000) {
    lastSend = millis();

    // ========== Sensor Data (V10-V14) ==========
    // Send 5 different sensor readings

    // Simulate temperature sensors
    float temp1 = random(200, 300) / 10.0;  // 20-30°C
    float temp2 = random(250, 350) / 10.0;  // 25-35°C
    float temp3 = random(180, 280) / 10.0;  // 18-28°C

    iot.gauge("V10", temp1);  // Temperature sensor 1
    iot.gauge("V11", temp2);  // Temperature sensor 2
    iot.gauge("V12", temp3);  // Temperature sensor 3

    // Simulate humidity sensors
    float humidity = random(400, 800) / 10.0;  // 40-80%
    iot.gauge("V13", humidity);

    // Simulate light sensor
    float light = random(0, 1000);  // 0-1000 lux
    iot.gauge("V14", light);

    // ========== LED Status (V20-V24) ==========
    // Send LED states back to app

    char pin[8];
    for (int i = 0; i < 5; i++) {
      snprintf(pin, sizeof(pin), "V%d", 20 + i);
      bool ledState = digitalRead(LED_PINS[i]);
      iot.led(pin, ledState);
    }

    // Print summary
    Serial.println("Sensors: Temp1=" + String(temp1) + " Temp2=" + String(temp2) +
                   " Temp3=" + String(temp3) + " Humidity=" + String(humidity) +
                   " Light=" + String(light));
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
 *    === Control Section ===
 *    Pin V0-V4: BUTTON widgets (LED 0-4 on/off)
 *    Pin V50: BUTTON widget (Master control - all LEDs)
 *    Pin V60-V61: SLIDER widgets (LED brightness 0-255)
 *    Pin V100: SLIDER widget (Update interval 1-60 seconds)
 *    Pin V101: SLIDER widget (Threshold 0-100)
 *    Pin V124: BUTTON widget (Factory Reset)
 *
 *    === Display Section ===
 *    Pin V10: GAUGE widget (Temperature 1, range 0-50°C)
 *    Pin V11: GAUGE widget (Temperature 2, range 0-50°C)
 *    Pin V12: GAUGE widget (Temperature 3, range 0-50°C)
 *    Pin V13: GAUGE widget (Humidity, range 0-100%)
 *    Pin V14: GAUGE widget (Light, range 0-1000 lux)
 *    Pin V20-V24: LED widgets (LED status indicators)
 *
 * 4. Control LEDs, adjust settings, monitor sensors!
 * 5. Test factory reset on V124
 *
 * ========================================
 * Pin Usage Summary:
 * ========================================
 *
 * This example uses only 22 pins out of 125 available!
 *
 * V0-V4:     Button controls (5 pins)
 * V10-V14:   Sensor data (5 pins)
 * V20-V24:   LED status (5 pins)
 * V50:       Master control (1 pin)
 * V60-V61:   Brightness sliders (2 pins)
 * V100-V101: Settings (2 pins)
 * V124:      Factory reset (1 pin)
 *
 * You still have 103 pins available for expansion!
 *
 * ========================================
 * Real-World Use Cases:
 * ========================================
 *
 * - Smart Home: Control 125 different devices
 * - Industrial: Monitor 125 different sensors
 * - Agriculture: 125 zones with sensors and valves
 * - Building Automation: 125 rooms or areas
 * - Custom Projects: Mix and match as needed!
 *
 * ========================================
 */
