/**
 * ============================================================
 *   ThingsLinker — BLE Provisioning
 * ============================================================
 *
 *  Demonstrates the full first-boot BLE provisioning flow.
 *
 *  First boot (no WiFi saved):
 *    1. Library starts BLE automatically.
 *    2. BLE device appears as "ThingsLinker_XXXXXX" (last 6 MAC digits).
 *    3. Open the ThingsLinker app → Add Device → scan BLE.
 *    4. App sends WiFi credentials over BLE.
 *    5. Device connects to WiFi + MQTT, BLE stops.
 *    6. Device is ready.
 *
 *  Subsequent boots:
 *    - WiFi credentials are saved in flash → direct connect, no BLE.
 *    - Hold BOOT button (GPIO 0) > 3 seconds to reset WiFi and
 *      re-provision (useful when changing networks).
 *
 *  App widgets (configure in ThingsLinker portal):
 *    V0  Button  → toggles built-in LED
 *    V1  LED     → shows current LED state
 *
 *  Hardware: any ESP32 board.
 *    Built-in LED: GPIO 2   (change LED_PIN if different)
 *    BOOT button:  GPIO 0   (hold > 3 s to reset WiFi)
 *
 *  CREDENTIALS: ThingsLinker org portal →
 *    Blueprints → [Blueprint] → Devices → [Device]
 * ============================================================
 */

#include <ThingsLinker.h>

// ── Credentials ──────────────────────────────────────────────
const char* AUTH_TOKEN   = "YOUR_AUTH_TOKEN";
const char* BLUEPRINT_ID = "YOUR_BLUEPRINT_ID";

// ── Hardware ─────────────────────────────────────────────────
#define LED_PIN   2   // Built-in LED on most ESP32 boards
#define BOOT_PIN  0   // BOOT button — hold > 3 s to reset WiFi

// ── ThingsLinker ─────────────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// ── State ─────────────────────────────────────────────────────
bool ledOn = false;

// ── Callbacks ─────────────────────────────────────────────────

// V0: Button from app toggles built-in LED
void onAppButton(bool pressed) {
  ledOn = pressed;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  iot.led("V1", ledOn);   // mirror state to LED widget
  Serial.println("[V0 Button] LED: " + String(ledOn ? "ON" : "OFF"));
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println(  "║   ThingsLinker — BLE Provisioning Demo   ║");
  Serial.println(  "╚══════════════════════════════════════════╝\n");

  // Hardware setup
  pinMode(LED_PIN,  OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 3× blink confirms power-on
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(150);
    digitalWrite(LED_PIN, LOW);  delay(150);
  }

  // Initialise ThingsLinker.
  // • If no WiFi credentials are saved → BLE provisioning starts automatically.
  // • If credentials are saved → connects to WiFi + MQTT directly.
  // Hold BOOT for 3 s to reset WiFi. Reset WiFi in the app or the User
  // Portal does the same (turn that off with iot.remoteReset(false)).
  iot.resetButton(BOOT_PIN);

  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Register app widget callbacks
  iot.onButton("V0", onAppButton);

  Serial.println("[Setup] Complete.");
  Serial.println("  First time? Open ThingsLinker app → Add Device → scan BLE.");
  Serial.println("  Already provisioned? Device auto-connects to WiFi.");
  Serial.println("  Hold BOOT (GPIO 0) > 3 s to reset WiFi and re-provision.\n");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  iot.run();   // also watches the BOOT button (iot.resetButton)

  delay(10);
}
