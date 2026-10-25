/**
 * ============================================================
 *   ThingsLinker — Full Feature Test
 * ============================================================
 *
 *  Exercises every library API in one sketch:
 *    - setBLEName()         Custom BLE brand name
 *    - debug()              Runtime debug toggle
 *    - begin() / run()      Lifecycle
 *    - All publish widgets  gauge, chart, display, label,
 *                           led, button, slider, send
 *    - All subscribe types  onButton, onSwitch, onSlider, onValue
 *    - Storage API          saveString/Int/Float/Bool, get*, hasKey,
 *                           removeKey, clearAllData
 *    - Status API           wifiConnected, mqttConnected, bleActive,
 *                           getIP, getChipID
 *    - resetButton()        Hold BOOT (>3 s) to reset WiFi (also from the app/portal)
 *    - NTP timestamp        Printed at first valid sync
 *
 *  No real sensors needed — all values are simulated.
 *
 *  Widget pin map:
 *  ┌─────┬────────────────┬────────────┬──────────────────────────────┐
 *  │ Pin │ Widget         │ Direction  │ Description                  │
 *  ├─────┼────────────────┼────────────┼──────────────────────────────┤
 *  │ V0  │ Button         │ App→Device │ Toggle built-in LED          │
 *  │ V1  │ Switch         │ App→Device │ Relay on/off                 │
 *  │ V2  │ Slider         │ App→Device │ Brightness 0–100             │
 *  │ V3  │ Value Display  │ App→Device │ Custom threshold from app    │
 *  │ V4  │ Gauge          │ Device→App │ Temperature (°C)             │
 *  │ V5  │ Chart          │ Device→App │ Humidity (%)                 │
 *  │ V6  │ Value Display  │ Device→App │ CO₂ ppm — via display()      │
 *  │ V7  │ Label          │ Device→App │ Pressure hPa — via label()   │
 *  │ V8  │ LED            │ Device→App │ Mirrors V0 button state      │
 *  │ V9  │ Button         │ Device→App │ Heartbeat pulse — via button()│
 *  │ V10 │ Slider         │ Device→App │ Battery % echo — via slider() │
 *  │ V11 │ Value Display  │ Device→App │ Uptime (s) — via send()      │
 *  └─────┴────────────────┴────────────┴──────────────────────────────┘
 *
 *  Hardware: any ESP32 board.
 *    Built-in LED is used (GPIO 2 on most boards).
 *    Change LED_PIN and BOOT_PIN below if needed.
 *
 *  CREDENTIALS: Get from the ThingsLinker organisation portal:
 *    Blueprints → [Blueprint] → Devices → [Device]
 * ============================================================
 */

#include <ThingsLinker.h>

// ── Credentials ──────────────────────────────────────────────
// Replace with your values from the ThingsLinker portal
const char* AUTH_TOKEN   = "YOUR_AUTH_TOKEN";
const char* BLUEPRINT_ID = "YOUR_BLUEPRINT_ID";

// ── Hardware ─────────────────────────────────────────────────
#define LED_PIN   2   // Built-in LED — change if your board differs
#define BOOT_PIN  0   // BOOT / GPIO0 button — standard on most DevKit boards

// ── Custom BLE brand name ─────────────────────────────────────
// Device will appear in the app as "AcmeSensors_<ChipID>"
// Change this to your company or product name.
#define BLE_BRAND_NAME  "AcmeSensors"

// ── Storage keys (max 15 chars each) ─────────────────────────
#define KEY_BOOT_COUNT  "boot_count"   // int   — increments every power-on
#define KEY_THRESHOLD   "threshold"    // float — last app-set threshold (V3)
#define KEY_DEVICE_NAME "device_name"  // string — set once, read every boot
#define KEY_RELAY_STATE "relay_state"  // bool  — persists relay on/off

// ── ThingsLinker instance ─────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// ── App→Device state ─────────────────────────────────────────
bool  lightOn    = false;   // V0 Button
bool  relayOn    = false;   // V1 Switch
float brightness = 50.0f;   // V2 Slider (0–100)
float threshold  = 100.0f;  // V3 Value Display (custom threshold)

// ── Simulated sensor values ───────────────────────────────────
float temperature = 24.0f;   // V4 Gauge   (°C)
float humidity    = 55.0f;   // V5 Chart   (%)
float co2         = 750.0f;  // V6 display (ppm)
float pressure    = 1013.0f; // V7 label   (hPa)
float batteryPct  = 85.0f;   // V10 slider (%)

// ── Timing ───────────────────────────────────────────────────
unsigned long lastPublish    = 0;
unsigned long lastStatusPrint = 0;
bool          ntpReported    = false;   // Print NTP sync once
bool          heartbeat      = false;   // V9 toggle
bool          _prevMQTTConn  = false;   // Reconnect detection

const unsigned long PUBLISH_INTERVAL = 5000;   // Sensor data every 5 s
const unsigned long STATUS_INTERVAL  = 15000;  // Status summary every 15 s

// ─────────────────────────────────────────────────────────────
// Helper: smooth random drift within [minV, maxV]
// ─────────────────────────────────────────────────────────────
float drift(float v, float minV, float maxV, float step) {
  float delta = ((float)(random(-100, 101))) / 100.0f * step;
  v += delta;
  if (v < minV) v = minV;
  if (v > maxV) v = maxV;
  return v;
}

// ─────────────────────────────────────────────────────────────
// V0: Button — toggles built-in LED + mirrors to V8 LED widget
// ─────────────────────────────────────────────────────────────
void onLightButton(bool pressed) {
  lightOn = pressed;
  digitalWrite(LED_PIN, lightOn ? HIGH : LOW);
  iot.led("V8", lightOn);   // Mirror state to V8 LED widget
  Serial.println("[V0 Button] Light " + String(lightOn ? "ON" : "OFF"));
}

// ─────────────────────────────────────────────────────────────
// V1: Switch — relay control (persists state to flash)
// ─────────────────────────────────────────────────────────────
void onRelaySwitch(bool on) {
  relayOn = on;
  iot.saveBool(KEY_RELAY_STATE, relayOn);   // Persist across reboots
  // digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);  // Uncomment for real relay
  Serial.println("[V1 Switch] Relay " + String(relayOn ? "ON" : "OFF")
                 + " (saved to flash)");
}

// ─────────────────────────────────────────────────────────────
// V2: Slider — brightness control
// ─────────────────────────────────────────────────────────────
void onBrightness(float value) {
  brightness = value;
  // analogWrite(PWM_PIN, (int)(brightness / 100.0f * 255));  // Uncomment for PWM
  Serial.println("[V2 Slider] Brightness: " + String((int)brightness) + "%");
}

// ─────────────────────────────────────────────────────────────
// V3: Value Display — custom threshold from app
// ─────────────────────────────────────────────────────────────
void onThreshold(float value) {
  threshold = value;
  iot.saveFloat(KEY_THRESHOLD, threshold);   // Persist
  Serial.println("[V3 Value] Threshold set to: " + String(threshold));
}

// ─────────────────────────────────────────────────────────────
// Publish all sensor + status data to the app
// ─────────────────────────────────────────────────────────────
void publishSensorData() {
  // Drift simulated values
  temperature = drift(temperature, 18.0f, 38.0f, 0.5f);
  humidity    = drift(humidity,    30.0f, 85.0f, 1.0f);
  co2         = drift(co2,        400.0f, 2000.0f, 25.0f);
  pressure    = drift(pressure,  1005.0f, 1025.0f, 0.3f);
  batteryPct  = drift(batteryPct,  10.0f,  100.0f, 0.2f);

  // V4 — Gauge: temperature
  iot.gauge("V4", temperature);

  // V5 — Chart: humidity (stores history in portal)
  iot.chart("V5", humidity);

  // V6 — Value Display: CO₂ via display()
  iot.display("V6", co2);

  // V7 — Label: pressure via label()
  iot.label("V7", pressure);

  // V8 — LED: already mirrors V0; re-publish to keep in sync after reconnect
  iot.led("V8", lightOn);

  // V9 — Button: heartbeat pulse (toggles true/false every interval)
  heartbeat = !heartbeat;
  iot.button("V9", heartbeat);

  // V10 — Slider: battery percentage via slider()
  iot.slider("V10", batteryPct);

  // V11 — Value Display: uptime in seconds via send() (generic publish)
  iot.send("V11", (float)(millis() / 1000UL));

  // Alert: check simulated value against stored threshold
  if (co2 > threshold) {
    Serial.println("[ALERT] CO2 " + String((int)co2) + " ppm exceeds threshold "
                   + String((int)threshold) + " ppm!");
  }

  Serial.println("─────────────────────────────────────────────");
  Serial.printf("[Publish] Temp:%.1f°C  Hum:%.0f%%  CO2:%.0f ppm  "
                "Pres:%.1f hPa  Bat:%.0f%%\n",
                temperature, humidity, co2, pressure, batteryPct);
  Serial.println("─────────────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────
// Print connection status and NTP info periodically
// ─────────────────────────────────────────────────────────────
void printStatus() {
  Serial.println("\n======= STATUS =======");
  Serial.println("Chip ID   : " + iot.getChipID());
  Serial.println("WiFi      : " + String(iot.wifiConnected() ? "Connected" : "Disconnected"));
  if (iot.wifiConnected()) {
    Serial.println("IP        : " + iot.getIP());
  }
  Serial.println("MQTT      : " + String(iot.mqttConnected() ? "Connected" : "Disconnected"));
  Serial.println("BLE       : " + String(iot.bleActive()     ? "Active"    : "Idle"));
  Serial.println("Uptime    : " + String(millis() / 1000UL) + " s");
  Serial.println("Light     : " + String(lightOn  ? "ON"  : "OFF"));
  Serial.println("Relay     : " + String(relayOn  ? "ON"  : "OFF"));
  Serial.printf("Brightness: %.0f%%\n", brightness);
  Serial.printf("Threshold : %.1f ppm\n", threshold);
  Serial.println("======================\n");

  // Report NTP sync on first valid timestamp
  if (!ntpReported) {
    time_t now = time(nullptr);
    if (now > 1577836800UL) {   // Valid if after 2020-01-01
      ntpReported = true;
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));
      Serial.println("[NTP] Synced! Current time: " + String(buf));
    } else {
      Serial.println("[NTP] Not yet synced — using millis() fallback for timestamps");
    }
  }
}

// ─────────────────────────────────────────────────────────────
// Run storage self-test (called once after WiFi/MQTT connect)
// ─────────────────────────────────────────────────────────────
void storageTest() {
  Serial.println("\n── Storage Self-Test ──────────────────────");

  // String
  iot.saveString(KEY_DEVICE_NAME, "FeatureTestDevice");
  String name = iot.getString(KEY_DEVICE_NAME, "Unknown");
  Serial.println("[Storage] String: " + name
                 + (name == "FeatureTestDevice" ? " ✓" : " ✗ FAIL"));

  // Int (boot counter — increments each run)
  int boots = iot.getInt(KEY_BOOT_COUNT, 0) + 1;
  iot.saveInt(KEY_BOOT_COUNT, boots);
  int readBack = iot.getInt(KEY_BOOT_COUNT, 0);
  Serial.println("[Storage] Int   : boot_count = " + String(readBack)
                 + (readBack == boots ? " ✓" : " ✗ FAIL"));

  // Float
  iot.saveFloat(KEY_THRESHOLD, 500.0f);
  float fv = iot.getFloat(KEY_THRESHOLD, 0.0f);
  Serial.println("[Storage] Float : threshold = " + String(fv)
                 + (abs(fv - 500.0f) < 0.01f ? " ✓" : " ✗ FAIL"));

  // Bool
  iot.saveBool(KEY_RELAY_STATE, true);
  bool bv = iot.getBool(KEY_RELAY_STATE, false);
  Serial.println("[Storage] Bool  : relay_state = " + String(bv ? "true" : "false")
                 + (bv ? " ✓" : " ✗ FAIL"));

  // hasKey / removeKey
  bool has = iot.hasKey(KEY_DEVICE_NAME);
  Serial.println("[Storage] hasKey(device_name) = " + String(has ? "true" : "false")
                 + (has ? " ✓" : " ✗ FAIL"));

  iot.removeKey(KEY_DEVICE_NAME);
  bool gone = !iot.hasKey(KEY_DEVICE_NAME);
  Serial.println("[Storage] removeKey → gone = " + String(gone ? "true" : "false")
                 + (gone ? " ✓" : " ✗ FAIL"));

  // Restore threshold from flash (may have been set by app via V3)
  threshold = iot.getFloat(KEY_THRESHOLD, 100.0f);
  Serial.println("[Storage] Restored threshold: " + String(threshold));

  Serial.println("───────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n");
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║   ThingsLinker — Full Feature Test       ║");
  Serial.println("╚══════════════════════════════════════════╝");

  // Hardware init
  pinMode(LED_PIN,  OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 3× blink — confirms power-on
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(150);
    digitalWrite(LED_PIN, LOW);  delay(150);
  }

  // ── BLE brand name (must be called BEFORE begin()) ──────────
  // The device will appear as "AcmeSensors_<ChipID>" in BLE scans.
  // Remove or change to "ThingsLinker" (the default) if not needed.
  iot.setBLEName(BLE_BRAND_NAME);

  // ── Optional: disable debug output for production builds ────
  // iot.debug(false);   // Uncomment to silence all library Serial output

  // Hold BOOT for 3 s to reset WiFi. Reset WiFi in the app or the User
  // Portal does the same (turn that off with iot.remoteReset(false)).
  iot.resetButton(BOOT_PIN);

  // ── Start ThingsLinker ──────────────────────────────────────
  // Handles BLE provisioning (first boot) or auto WiFi + MQTT connect.
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // ── Subscribe: App → Device ─────────────────────────────────
  iot.onButton("V0", onLightButton);  // Button  — toggle LED
  iot.onSwitch("V1", onRelaySwitch);  // Switch  — relay
  iot.onSlider("V2", onBrightness);   // Slider  — brightness
  iot.onValue ("V3", onThreshold);    // Value Display — custom threshold

  // ── Storage self-test (runs once on every boot) ─────────────
  storageTest();

  // ── Widget pin map ──────────────────────────────────────────
  Serial.println("[Widget Map]");
  Serial.println("  V0  → Button        App→Device   Toggle LED");
  Serial.println("  V1  → Switch        App→Device   Relay on/off");
  Serial.println("  V2  → Slider        App→Device   Brightness 0–100");
  Serial.println("  V3  → Value Display App→Device   CO₂ threshold");
  Serial.println("  V4  → Gauge         Device→App   Temperature °C");
  Serial.println("  V5  → Chart         Device→App   Humidity %");
  Serial.println("  V6  → Value Display Device→App   CO₂ ppm");
  Serial.println("  V7  → Label         Device→App   Pressure hPa");
  Serial.println("  V8  → LED           Device→App   Mirrors V0 state");
  Serial.println("  V9  → Button        Device→App   Heartbeat pulse");
  Serial.println("  V10 → Slider        Device→App   Battery %");
  Serial.println("  V11 → Value Display Device→App   Uptime (s)");
  Serial.println();
  Serial.println("  BLE name : " + String(BLE_BRAND_NAME) + "_" + iot.getChipID());
  Serial.println("  Chip ID  : " + iot.getChipID());
  Serial.println("  Hold BOOT >3 s to reset WiFi and re-start BLE");
  Serial.println("───────────────────────────────────────────\n");

  // Push initial state immediately — don't wait for first 5-second interval
  if (iot.mqttConnected()) {
    publishSensorData();
    _prevMQTTConn = true;
  }
}

// ─────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  // Must be called every iteration — drives WiFi, MQTT, BLE, reconnect
  iot.run();

  bool nowConn = iot.mqttConnected();

  // Detect fresh MQTT connect (first boot or after reconnect) and push
  // current state immediately so the app doesn't see stale values.
  if (nowConn && !_prevMQTTConn) {
    Serial.println("[Reconnect] MQTT connected — pushing current state…");
    publishSensorData();
    lastPublish = millis();
  }
  _prevMQTTConn = nowConn;

  unsigned long now = millis();

  // Publish sensor data every PUBLISH_INTERVAL ms (only when MQTT is up)
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    if (nowConn) {
      publishSensorData();
    }
  }

  // Print status summary every STATUS_INTERVAL ms
  if (now - lastStatusPrint >= STATUS_INTERVAL) {
    lastStatusPrint = now;
    printStatus();
  }

  delay(10);
}
