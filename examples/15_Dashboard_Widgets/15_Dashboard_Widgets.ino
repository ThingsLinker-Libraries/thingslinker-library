/**
 * ============================================================
 *   ThingsLinker — Dashboard Widgets
 *   Status, Progress Bar, Selector, Number Input,
 *   Text Input and Keypad
 * ============================================================
 *
 *  Shows how the device talks to the newer dashboard widgets.
 *  All values are simulated, so no extra hardware is needed.
 *
 *  Add these widgets to the device's blueprint in the
 *  Organisation Portal, on the pins below:
 *
 *  ┌─────┬──────────────┬────────────┬───────────┬─────────────────────────────┐
 *  │ Pin │ Widget       │ Direction  │ Data type │ What it does here           │
 *  ├─────┼──────────────┼────────────┼───────────┼─────────────────────────────┤
 *  │ V20 │ Status       │ Device→App │ Float     │ A level 0–100, shown as a   │
 *  │     │              │            │           │ named state (Low/Normal/…)  │
 *  │ V21 │ Status       │ Device→App │ String    │ A state code: IDLE/RUN/ERR  │
 *  │ V22 │ Progress Bar │ Device→App │ Float     │ The same level as a bar     │
 *  │ V23 │ Selector     │ App↔Device │ Integer   │ Mode 0, 1 or 2              │
 *  │ V24 │ Number Input │ App↔Device │ Float     │ A set point                 │
 *  │ V25 │ Text Input   │ App→Device │ String    │ A line of text              │
 *  │ V26 │ Keypad       │ App→Device │ String    │ Commands: start/stop/reset  │
 *  │ V27 │ Label        │ Device→App │ String    │ Echo of the last command    │
 *  └─────┴──────────────┴────────────┴───────────┴─────────────────────────────┘
 *
 *  Portal settings that this sketch expects:
 *    V20 Status   states with ranges, e.g. below 30 "Low", 30–70 "Normal",
 *                 70 and above "High"
 *    V21 Status   states matching exactly IDLE, RUN and ERR
 *    V22 Progress minimum 0, maximum 100
 *    V23 Selector options with values 0, 1 and 2 (any labels you like)
 *    V24 Number   minimum 0, maximum 100, step 0.5
 *    V26 Keypad   buttons with values start, stop and reset
 *
 *  Widgets that need NO code on the device — add them in the
 *  portal and they work from the pins above:
 *    Computed Value   e.g. formula  V22 * V24 / 100
 *    Multi Chart      e.g. series V20 and V24 on one chart
 *    Event Log        a running list of what the pins received
 *    Table            every widget's current value in one list
 *    Text             a heading or a note
 *
 *  Hardware: any ESP32 board.
 *    BOOT button: GPIO 0 (hold >3 s to reset WiFi)
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
#define BOOT_PIN  0   // BOOT button — hold >3 s to reset WiFi

// ── Virtual pins ─────────────────────────────────────────────
#define PIN_LEVEL_STATUS  "V20"   // Status (number → state by range)
#define PIN_STATE_STATUS  "V21"   // Status (text → state by exact match)
#define PIN_PROGRESS      "V22"   // Progress Bar
#define PIN_MODE          "V23"   // Selector
#define PIN_SETPOINT      "V24"   // Number Input
#define PIN_TEXT          "V25"   // Text Input
#define PIN_KEYPAD        "V26"   // Keypad
#define PIN_LAST_COMMAND  "V27"   // Label

// ── ThingsLinker ─────────────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// ── State ────────────────────────────────────────────────────
int   mode      = 0;        // chosen with the Selector
float setpoint  = 50.0f;    // chosen with the Number Input
bool  running   = false;    // changed with the Keypad
float level     = 40.0f;    // simulated reading, 0–100

// ── Timing ───────────────────────────────────────────────────
const unsigned long PUBLISH_INTERVAL = 5000;

unsigned long lastPublish    = 0;
bool          wasConnected   = false;


// ── Callbacks: App → Device ──────────────────────────────────

// V23 Selector — the value of the option picked (0, 1 or 2).
// A setting: the server keeps the last choice and delivers it again whenever
// the device connects, so the device always ends up in the chosen mode.
void onMode(float option) {
  mode = (int)option;
  iot.saveInt("mode", mode);
  Serial.printf("[V23 Selector] Mode: %d\n", mode);
  // Do not call iot.selector(PIN_MODE, ...) here: the device hears its own
  // messages on this pin and the callback would run again.
}

// V24 Number Input — the number typed or stepped to in the app.
void onSetpoint(float value) {
  setpoint = value;
  iot.saveFloat("setpoint", setpoint);
  Serial.printf("[V24 Number Input] Set point: %.1f\n", setpoint);
}

// V25 Text Input — a line of text. The pointer is only valid here,
// so copy the text if you need it later.
void onText(const char* text) {
  Serial.printf("[V25 Text Input] \"%s\"\n", text);
  iot.label(PIN_LAST_COMMAND, String("Text: ") + text);
}

// V26 Keypad — the value of the button pressed. Every press arrives once,
// including the same button twice in a row, and is not repeated when the
// device reconnects.
void onKey(const char* key) {
  if      (strcmp(key, "start") == 0) running = true;
  else if (strcmp(key, "stop")  == 0) running = false;
  else if (strcmp(key, "reset") == 0) { running = false; level = 0.0f; }
  else {
    Serial.printf("[V26 Keypad] Unknown key \"%s\"\n", key);
    return;
  }

  Serial.printf("[V26 Keypad] %s → %s\n", key, running ? "running" : "stopped");
  iot.label (PIN_LAST_COMMAND, String("Key: ") + key);
  iot.status(PIN_STATE_STATUS, running ? "RUN" : "IDLE");
}

// ── Publish: Device → App ────────────────────────────────────
void publishAll() {
  // Simulated reading: climbs towards the set point while running,
  // faster in a higher mode, and falls back slowly when stopped.
  if (running) level += (setpoint - level) * 0.1f * (mode + 1);
  else         level -= 1.0f;
  if (level < 0.0f)   level = 0.0f;
  if (level > 100.0f) level = 100.0f;

  // Status takes any kind of value. The dashboard decides which state to
  // show from the rules set in the portal; the device just sends the value.
  iot.status  (PIN_LEVEL_STATUS, level);                    // number → range rule
  iot.status  (PIN_STATE_STATUS, running ? "RUN" : "IDLE"); // text → exact match
  iot.progress(PIN_PROGRESS,     level);

  Serial.printf("[Publish] Level: %.1f  State: %s  Mode: %d  Set point: %.1f\n",
                level, running ? "RUN" : "IDLE", mode, setpoint);
}

// Show the app what the device is actually using (after a boot the device
// restores its own saved settings).
void publishSettings() {
  iot.selector   (PIN_MODE,     mode);
  iot.numberInput(PIN_SETPOINT, setpoint);
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println(  "║   ThingsLinker — Dashboard Widgets       ║");
  Serial.println(  "╚══════════════════════════════════════════╝\n");

  // Hold BOOT for 3 s to reset WiFi. Reset WiFi in the app or the User
  // Portal does the same (turn that off with iot.remoteReset(false)).
  iot.resetButton(BOOT_PIN);

  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // Settings survive a reboot
  mode     = iot.getInt  ("mode",     0);
  setpoint = iot.getFloat("setpoint", 50.0f);

  // ── Subscribe: App → Device ──────────────────────────────────
  iot.onSelector   (PIN_MODE,     onMode);      // Selector with number values
  iot.onNumberInput(PIN_SETPOINT, onSetpoint);  // Number Input
  iot.onTextInput  (PIN_TEXT,     onText);      // Text Input
  iot.onKeypadText (PIN_KEYPAD,   onKey);       // Keypad with text values
  // For a Selector with text values use iot.onSelectorText(), and for a
  // Keypad with number values use iot.onKeypad().

  Serial.println("[Widgets] Registered:");
  Serial.println("  V20 Status        Device→App  Level as a named state");
  Serial.println("  V21 Status        Device→App  IDLE / RUN / ERR");
  Serial.println("  V22 Progress Bar  Device→App  Level 0–100");
  Serial.println("  V23 Selector      App↔Device  Mode 0 / 1 / 2");
  Serial.println("  V24 Number Input  App↔Device  Set point");
  Serial.println("  V25 Text Input    App→Device  A line of text");
  Serial.println("  V26 Keypad        App→Device  start / stop / reset");
  Serial.println("  V27 Label         Device→App  Last command received");
  Serial.println("\n  Hold BOOT >3 s to reset WiFi");
  Serial.println("───────────────────────────────────────────\n");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  iot.run();

  bool connected = iot.mqttConnected();

  // On every (re)connect: push the current state
  if (connected && !wasConnected) {
    Serial.println("[MQTT] Connected — pushing state...");
    publishSettings();
    publishAll();
    lastPublish = millis();
  }
  wasConnected = connected;

  if (connected && millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();
    publishAll();
  }

  delay(10);
}
