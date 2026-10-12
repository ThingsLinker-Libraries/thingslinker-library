/**
 * @file 16_GSM_Dashboard_Widgets.ino
 * @brief ThingsLinker GSM — Status, Progress Bar, Selector, Number Input,
 *        Text Input and Keypad widgets over 4G
 *
 * The same widgets as 15_Dashboard_Widgets, using the ThingsLinkerGSM class
 * with a Simcom A7672 / SIM7672 modem. The widget functions are identical in
 * both classes. All values are simulated.
 *
 * ── Widgets to add in the Organisation Portal ────────────────────────────────
 *   Pin  Widget        Direction   Data type  What it does here
 *   V20  Status        Device→App  Float      A level 0–100 as a named state
 *   V21  Status        Device→App  String     A state code: IDLE / RUN / ERR
 *   V22  Progress Bar  Device→App  Float      The same level as a bar
 *   V23  Selector      App↔Device  Integer    Mode 0, 1 or 2
 *   V24  Number Input  App↔Device  Float      A set point
 *   V25  Text Input    App→Device  String     A line of text
 *   V26  Keypad        App→Device  String     Commands: start / stop / reset
 *   V27  Label         Device→App  String     Echo of the last command
 *
 *   Portal settings this sketch expects:
 *     V20 Status    states with ranges (e.g. below 30, 30–70, 70 and above)
 *     V21 Status    states matching exactly IDLE, RUN and ERR
 *     V22 Progress  minimum 0, maximum 100
 *     V23 Selector  options with values 0, 1 and 2
 *     V24 Number    minimum 0, maximum 100, step 0.5
 *     V26 Keypad    buttons with values start, stop and reset
 *
 *   Computed Value, Multi Chart, Event Log, Table and Text widgets need no
 *   code on the device: add them in the portal and they work from these pins.
 *
 * ── Wiring ────────────────────────────────────────────────────────────────────
 *   ESP32 GPIO16 (RX2) ────── A7672 TX
 *   ESP32 GPIO17 (TX2) ────── A7672 RX
 *   ESP32 GPIO4        ────── A7672 PWRKEY
 *   ESP32 GND          ────── A7672 GND
 *   (see 12_GSM_Connection for power notes)
 *
 * ── Getting your credentials ─────────────────────────────────────────────────
 *   Organisation Portal → Blueprints → [Blueprint] → Devices → [Device]
 */

// ─────────────────────────────────────────────────────────────────────────────
// 1. Credentials — replace with your actual values
// ─────────────────────────────────────────────────────────────────────────────
#define AUTH_TOKEN    "your_device_auth_token"
#define BLUEPRINT_KEY "your_blueprint_key"       // starts with "BLUE"
#define CLIENT_KEY    "your_client_key"
#define SECRET_KEY    "your_secret_key"

// ─────────────────────────────────────────────────────────────────────────────
// 2. APN — set to your SIM card's APN
// ─────────────────────────────────────────────────────────────────────────────
#define APN           "airtelgprs.com"
#define APN_USER      ""
#define APN_PASS      ""

// ─────────────────────────────────────────────────────────────────────────────
// 3. Hardware pins
// ─────────────────────────────────────────────────────────────────────────────
#define MODEM_RX_PIN   16   // ESP32 GPIO16 ← A7672 TX
#define MODEM_TX_PIN   17   // ESP32 GPIO17 → A7672 RX
#define MODEM_PWR_PIN   4   // GPIO 4 → A7672 PWRKEY (or TL_NO_PWRKEY)

#include <ThingsLinkerGSM.h>

ThingsLinkerGSM iot(Serial2, AUTH_TOKEN, BLUEPRINT_KEY);

// ─────────────────────────────────────────────────────────────────────────────
// Virtual pins
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_LEVEL_STATUS  "V20"   // Status (number → state by range)
#define PIN_STATE_STATUS  "V21"   // Status (text → state by exact match)
#define PIN_PROGRESS      "V22"   // Progress Bar
#define PIN_MODE          "V23"   // Selector
#define PIN_SETPOINT      "V24"   // Number Input
#define PIN_TEXT          "V25"   // Text Input
#define PIN_KEYPAD        "V26"   // Keypad
#define PIN_LAST_COMMAND  "V27"   // Label

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
int   mode     = 0;        // chosen with the Selector
float setpoint = 50.0f;    // chosen with the Number Input
bool  running  = false;    // changed with the Keypad
float level    = 40.0f;    // simulated reading, 0–100

// Mobile data costs money: publish less often than on WiFi
const unsigned long PUBLISH_INTERVAL = 15000;

unsigned long lastPublish  = 0;
bool          wasConnected = false;


// ─────────────────────────────────────────────────────────────────────────────
// Callbacks: App → Device
// ─────────────────────────────────────────────────────────────────────────────

// V23 Selector — a setting: the server keeps the last choice and delivers it
// again whenever the device connects.
// Do not call iot.selector() from here: the device hears its own messages on
// this pin and the callback would run again.
void onMode(float option) {
  mode = (int)option;
  iot.saveInt("mode", mode);
  Serial.printf("[V23 Selector] Mode: %d\n", mode);
}

// V24 Number Input
void onSetpoint(float value) {
  setpoint = value;
  iot.saveFloat("setpoint", setpoint);
  Serial.printf("[V24 Number Input] Set point: %.1f\n", setpoint);
}

// V25 Text Input — the pointer is only valid inside the callback
void onText(const char* text) {
  Serial.printf("[V25 Text Input] \"%s\"\n", text);
  iot.label(PIN_LAST_COMMAND, String("Text: ") + text);
}

// V26 Keypad — every press arrives once, including the same button twice.
// Presses are not repeated when the device reconnects.
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

// ─────────────────────────────────────────────────────────────────────────────
// Publish: Device → App
// ─────────────────────────────────────────────────────────────────────────────
void publishAll() {
  // Simulated reading: climbs towards the set point while running,
  // faster in a higher mode, and falls back slowly when stopped.
  if (running) level += (setpoint - level) * 0.3f * (mode + 1);
  else         level -= 3.0f;
  if (level < 0.0f)   level = 0.0f;
  if (level > 100.0f) level = 100.0f;

  iot.status  (PIN_LEVEL_STATUS, level);                    // number → range rule
  iot.status  (PIN_STATE_STATUS, running ? "RUN" : "IDLE"); // text → exact match
  iot.progress(PIN_PROGRESS,     level);

  Serial.printf("[Publish] Level: %.1f  State: %s  Mode: %d  Set point: %.1f  Signal: %d/31\n",
                level, running ? "RUN" : "IDLE", mode, setpoint, iot.signalQuality());
}

// Show the app what the device is actually using
void publishSettings() {
  iot.selector   (PIN_MODE,     mode);
  iot.numberInput(PIN_SETPOINT, setpoint);
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  // Initialise Serial2 for the modem BEFORE calling iot.begin()
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // Register callbacks BEFORE begin() — they are stored and subscribed again
  // after every MQTT reconnect.
  iot.onSelector   (PIN_MODE,     onMode);      // Selector with number values
  iot.onNumberInput(PIN_SETPOINT, onSetpoint);  // Number Input
  iot.onTextInput  (PIN_TEXT,     onText);      // Text Input
  iot.onKeypadText (PIN_KEYPAD,   onKey);       // Keypad with text values
  // For a Selector with text values use iot.onSelectorText(), and for a
  // Keypad with number values use iot.onKeypad().

  iot.begin(CLIENT_KEY, SECRET_KEY, APN, MODEM_PWR_PIN, APN_USER, APN_PASS);

  // Settings survive a reboot
  mode     = iot.getInt  ("mode",     0);
  setpoint = iot.getFloat("setpoint", 50.0f);

  if (iot.mqttConnected()) {
    Serial.println("\n>>> ThingsLinker GSM ready! <<<");
    Serial.println("IP     : " + iot.getIP());
    Serial.println("Signal : " + String(iot.signalQuality()) + "/31");
  } else {
    Serial.println("\n>>> Connection failed — check modem, SIM card, and APN <<<");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  iot.run();   // process MQTT messages + auto-reconnect

  bool connected = iot.mqttConnected();

  // On every (re)connect: push the current state
  if (connected && !wasConnected) {
    publishSettings();
    publishAll();
    lastPublish = millis();
  }
  wasConnected = connected;

  if (connected && millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();
    publishAll();
  }
}
