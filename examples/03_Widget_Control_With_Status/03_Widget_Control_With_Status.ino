/**
 * ThingsLinker - Widget Control with Online/Offline Status
 *
 * This example demonstrates:
 * - Automatic WiFi connection and MQTT setup
 * - Widget control (button controls LED)
 * - Sensor data publishing (temperature gauge)
 * - Automatic heartbeat/status messages (30 seconds)
 * - Online/offline status in mobile app
 *
 * Hardware:
 * - ESP32 board
 * - LED on GPIO 2 (built-in)
 *
 * Setup in ThingsLinker App:
 * 1. Create device and onboard via BLE
 * 2. Configure widgets:
 *    - Button widget on V0 (controls LED)
 *    - Gauge widget on V1 (shows temperature)
 * 3. Watch device status change to "Online" when connected
 */

#include <ThingsLinker.h>

// Device credentials (get from ThingsLinker portal)
const char* AUTH_TOKEN = "YOUR_DEVICE_AUTH_TOKEN";
const char* BLUEPRINT_ID = "YOUR_BLUEPRINT_ID";

// Hardware
#define LED_PIN 2

// ThingsLinker
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// Temperature simulation
float temperature = 25.0;
unsigned long lastTempUpdate = 0;
const unsigned long TEMP_UPDATE_INTERVAL = 5000; // Update every 5 seconds

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n========================================");
  Serial.println("  ThingsLinker - Widget Control + Status");
  Serial.println("========================================\n");

  // Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Start ThingsLinker (handles WiFi + MQTT automatically)
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Listen to button from app (V0)
  // When user presses button in app, LED turns on/off
  iot.onButton("V0", [](bool pressed) {
    digitalWrite(LED_PIN, pressed ? HIGH : LOW);
    Serial.println("[Button] LED " + String(pressed ? "ON" : "OFF"));
  });

  Serial.println("\n========================================");
  Serial.println("  READY - Device is Online!");
  Serial.println("========================================");
  Serial.println("✓ Sending heartbeat every 30 seconds");
  Serial.println("✓ App will show device status: ONLINE");
  Serial.println("\nWidgets configured:");
  Serial.println("- V0: Button (controls LED)");
  Serial.println("- V1: Gauge (temperature)");
  Serial.println("========================================\n");
}

void loop() {
  // Run ThingsLinker (handles MQTT, heartbeat, reconnection)
  iot.run();

  // Simulate temperature sensor (update every 5 seconds)
  unsigned long now = millis();
  if (now - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
    lastTempUpdate = now;

    // Simulate temperature changes
    temperature = 25.0 + random(-5, 5) * 0.1;

    // Send to app (V1)
    iot.gauge("V1", temperature);
    Serial.println("[Sensor] Temperature: " + String(temperature, 1) + "°C");
  }

  // Small delay
  delay(100);
}

/*
 * ========================================
 * What happens automatically:
 * ========================================
 *
 * 1. Device connects to WiFi (saved from BLE provisioning)
 * 2. Device connects to MQTT broker
 * 3. Device subscribes to button control topic (V0)
 * 4. Device sends heartbeat every 30 seconds:
 *    - Topic: device/status/{BlueprintId}/{AuthToken}/
 *    - Payload: "ONLINE" (simple string, retained)
 * 5. App receives heartbeat and shows "Online" status
 * 6. Temperature is published every 5 seconds to V1
 * 7. Button press in app controls LED via MQTT
 *
 * ========================================
 * Testing the Online/Offline Status:
 * ========================================
 *
 * ONLINE:
 * 1. Upload this sketch to ESP32
 * 2. Open Serial Monitor (115200 baud)
 * 3. Wait for "READY - Device is Online!" message
 * 4. Open ThingsLinker app
 * 5. Go to device widgets screen
 * 6. Watch top bar: Device status should show "Online" (green)
 * 7. Every 30 seconds, ESP32 logs: "[MQTT] ✓ Heartbeat sent"
 *
 * OFFLINE:
 * 1. Disconnect ESP32 power or WiFi
 * 2. Wait 60+ seconds (2 missed heartbeats)
 * 3. App should show "Offline" status (gray)
 *
 * ========================================
 */
