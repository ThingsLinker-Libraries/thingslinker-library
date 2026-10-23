/**
 * ========================================
 *   ThingsLinker - LED Control Example
 * ========================================
 *
 * Control an LED from the ThingsLinker app!
 * This example shows:
 * - How to control LED from app button
 * - How to send LED status to app
 */

#include <ThingsLinker.h>

// Your credentials (blueprintId is mandatory)
ThingsLinker iot("YOUR_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");

// LED pin
const int LED_PIN = 2;  // Built-in LED on most ESP32 boards

void setup() {
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  delay(1000);
  
  // Initialize ThingsLinker with MQTT credentials
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Listen for button press from app on pin V0
  iot.onButton("V0", [](bool value) {
    // Turn LED on/off based on button
    digitalWrite(LED_PIN, value ? HIGH : LOW);
    Serial.println(value ? "LED ON" : "LED OFF");
  });
}

void loop() {
  // Run ThingsLinker
  iot.run();

  // // Send LED status to app every 2 seconds
  // static unsigned long lastSend = 0;
  // if (millis() - lastSend > 2000) {
  //   lastSend = millis();

  //   // Read LED state and send to app
  //   bool ledState = digitalRead(LED_PIN);
  //   iot.led("V1", ledState);  // Send to pin V1 (LED widget in app)
  // }
}

/*
 * ========================================
 * How to use:
 * ========================================
 *
 * 1. Upload this code to ESP32
 * 2. Open ThingsLinker app
 * 3. Connect device via BLE (first time only)
 * 4. Go to device dashboard
 * 5. Add a BUTTON widget on pin V0
 * 6. Add a LED widget on pin V1
 * 7. Press button in app - LED will turn on/off!
 * 8. LED widget will show real-time LED status
 *
 * ========================================
 */
