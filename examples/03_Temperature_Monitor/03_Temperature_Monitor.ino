/**
 * ========================================
 *   ThingsLinker - Temperature Monitor
 * ========================================
 *
 * Send sensor data to ThingsLinker app!
 * This example shows:
 * - Reading temperature sensor
 * - Sending data to app gauge widget
 * - Sending data to app chart widget
 */

#include <ThingsLinker.h>

// Your credentials (blueprintId is mandatory)
ThingsLinker iot("YOUR_DEVICE_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");

void setup() {
  // Initialize ThingsLinker with MQTT credentials
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();
}

void loop() {
  // Run ThingsLinker
  iot.run();

  // Read temperature every 5 seconds
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 5000) {
    lastRead = millis();

    // Simulate temperature reading (replace with real sensor)
    float temperature = random(200, 350) / 10.0;  // Random value 20.0 - 35.0

    // Send to app
    iot.gauge("V0", temperature);  // Send to gauge widget on pin V0

    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
  }
}

/*
 * ========================================
 * How to use:
 * ========================================
 *
 * 1. Upload this code to ESP32
 * 2. Connect device via BLE (first time only)
 * 3. Go to device dashboard in app
 * 4. Add a GAUGE widget on pin V0
 * 5. Set gauge range: Min=0, Max=50
 * 6. Set unit: °C
 * 7. Watch real-time temperature!
 *
 * ========================================
 * Using Real Sensor:
 * ========================================
 *
 * Replace the simulated reading with real sensor:
 *
 * // For DHT11/DHT22:
 * #include <DHT.h>
 * DHT dht(4, DHT22);
 * float temperature = dht.readTemperature();
 *
 * // For DS18B20:
 * #include <OneWire.h>
 * #include <DallasTemperature.h>
 * OneWire oneWire(4);
 * DallasTemperature sensors(&oneWire);
 * sensors.requestTemperatures();
 * float temperature = sensors.getTempCByIndex(0);
 *
 * ========================================
 */
