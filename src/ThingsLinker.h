/**
 * @file ThingsLinker.h
 * @brief ThingsLinker IoT Library for ESP32
 *
 * Connect your ESP32 to the ThingsLinker platform in just 3 steps:
 *
 *   // 1. Create
 *   ThingsLinker iot("YOUR_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");
 *
 *   void setup() {
 *     Serial.begin(115200);
 *     // 2. Start (no login needed for the ThingsLinker broker)
 *     iot.begin();
 *   }
 *
 *   void loop() {
 *     // 3. Run
 *     iot.run();
 *   }
 *
 * The library handles everything automatically:
 *   - BLE provisioning (first boot — no WiFi credentials yet)
 *   - WiFi connection and reconnection
 *   - MQTT connection and reconnection
 *   - NTP time sync for accurate telemetry timestamps
 *   - Last Will and Testament for online/offline status
 *
 * Virtual pins V0–V124 (125 pins) match the ThingsLinker Arduino companion
 * library and the organisation portal widget configuration.
 *
 * Get credentials from the ThingsLinker organisation portal:
 *   Blueprints → [Blueprint] → Devices → [Device] → Auth Token / Blueprint Key
 *
 * No MQTT username or password is needed for the ThingsLinker broker; they
 * are optional (see begin()).
 */

#ifndef THINGSLINKER_H
#define THINGSLINKER_H

#include <Arduino.h>
#include "TL_Config.h"
#include "TL_Value.h"
#include "TL_BLE.h"
#include "TL_WiFi.h"
#include "TL_MQTT.h"
#include "TL_Storage.h"
#include "TL_OTA.h"
#include "TL_Offline.h"

class ThingsLinker {
public:

  // ── Construction ────────────────────────────────────────────────────────────

  /**
   * @param authToken   Device authentication token (from organisation portal)
   * @param blueprintId Blueprint ID (e.g. "BLUExxxxxxxxxxxxxxxx" — starts with "BLUE")
   */
  ThingsLinker(const char* authToken, const char* blueprintId);

  // ── Lifecycle ───────────────────────────────────────────────────────────────

  /**
   * Initialise the library. Call once in setup().
   *
   * Note: call Serial.begin(115200) in your sketch before begin() if you want
   * to see debug output from the very first lines.
   *
   * MQTT username and password are OPTIONAL. The ThingsLinker broker does not
   * need a login, so most sketches just call:
   *
   *   iot.begin();
   *
   * Pass them only if your own broker asks for one:
   *
   *   iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
   *
   * If you have an MQTT Client Key and Secret Key, the Client Key is the
   * username and the Secret Key is the password.
   *
   * @param mqttUsername  Optional broker username (leave out or "" for none)
   * @param mqttPassword  Optional broker password (leave out or "" for none)
   */
  void begin(const char* mqttUsername = nullptr, const char* mqttPassword = nullptr);

  /**
   * Process all library tasks. Call every iteration of loop().
   * Handles MQTT messages, auto-reconnect, and BLE provisioning.
   */
  void run();

  // ── Publish (device → app) ──────────────────────────────────────────────────
  //
  // Data types per widget (same rules as the portal "Data Type" setting):
  //
  //   Button, Switch, LED        Boolean
  //   Slider, Gauge, Chart       Integer or Float
  //   Value Display, Label       Integer, Float, Boolean or String
  //   Terminal, Text Input       String
  //   Status                     Integer, Float, Boolean or String
  //   Progress Bar, Number Input Integer or Float
  //   Selector, Keypad           Integer, Float or String
  //   RGB, Timer, Map, Joystick  structured (dedicated methods)
  //
  // Text, Table, Multi Chart, Computed Value and Event Log widgets need no
  // code on the device: they are built in the dashboard from the other pins.
  //
  // Methods taking a TLValue accept any of these directly — the data type is
  // taken from the argument type, e.g. 42 → Integer, 23.5f → Float,
  // true → Boolean, "Running" / String → String.

  /** Publish a Button state (1 = pressed, 0 = released).   Pin: V0–V124 */
  void button(const char* pin, bool value);

  /** Publish an LED state (1 = on, 0 = off).               Pin: V0–V124 */
  void led(const char* pin, bool value);

  /**
   * Publish GPS coordinates to a Map widget.
   * Sends {"v": lat, "lat": lat, "lng": lng, "t": timestamp}.
   *
   * Example:
   *   iot.map("V12", 23.0225f, 72.5714f);  // Ahmedabad, India
   */
  void map(const char* pin, float lat, float lng);

  /** Publish a value to a Gauge widget.                     Pin: V0–V124 */
  void gauge(const char* pin, float value);

  /** Publish a value to a Chart widget (stores history).    Pin: V0–V124 */
  void chart(const char* pin, float value);

  /**
   * Publish a value to a Value Display widget.              Pin: V0–V124
   * Accepts Integer, Float, Boolean or String.
   *
   * Example:
   *   iot.display("V3", 42);          // Integer
   *   iot.display("V3", 23.5f);       // Float
   *   iot.display("V3", "Running");   // String
   */
  void display(const char* pin, const TLValue& value);

  /**
   * Publish a value to a Label widget.                      Pin: V0–V124
   * Accepts Integer, Float, Boolean or String.
   *
   * Example:
   *   iot.label("V4", "Door open");
   *   iot.label("V4", String("Zone ") + zone);
   */
  void label(const char* pin, const TLValue& value);

  /**
   * Print a line to a Terminal widget.                      Pin: V0–V124
   * Numbers and booleans are accepted too and shown as text.
   *
   * The line is sent on the pin's display channel, not the Terminal control
   * channel, so it is never echoed back into your own onTerminal() callback.
   *
   * Example:
   *   iot.terminal("V5", "Boot complete");
   */
  void terminal(const char* pin, const TLValue& text);

  /** Publish a Slider position.                             Pin: V0–V124 */
  void slider(const char* pin, float value);

  /**
   * Generic publish — maps to Value Display widget.         Pin: V0–V124
   * Accepts Integer, Float, Boolean or String.
   */
  void send(const char* pin, const TLValue& value);

  /**
   * Publish a value to a Status widget.                     Pin: V0–V124
   * The dashboard shows the state (label, colour, icon) whose rule covers the
   * value. The states are set up in the portal, so the device only sends the
   * raw value: a number for range rules, or a number / text for exact matches.
   *
   * Example:
   *   iot.status("V7", 82);        // falls in whichever range covers 82
   *   iot.status("V7", "ERR");     // matches a state set to exactly "ERR"
   */
  void status(const char* pin, const TLValue& value);

  /**
   * Publish a value to a Progress Bar widget.               Pin: V0–V124
   * The bar's minimum and maximum are set in the portal.
   *
   * Example:
   *   iot.progress("V8", 63);
   */
  void progress(const char* pin, float value);

  /**
   * Tell a Selector widget which option is active now.      Pin: V0–V124
   * Send the option's value as it is set in the portal (a number or text).
   * Use it when the device changes the option by itself, e.g. after a boot or
   * a local button. See the note on onSelector() about echoes.
   *
   * Example:
   *   iot.selector("V9", 2);
   *   iot.selector("V9", "eco");
   */
  void selector(const char* pin, const TLValue& value);

  /**
   * Tell a Number Input widget the current number.          Pin: V0–V124
   *
   * Example:
   *   iot.numberInput("V10", 21.5f);
   */
  void numberInput(const char* pin, float value);

  /**
   * Publish to ANY widget type by name. Use this for widget types that have
   * no dedicated method, or types added to the platform later.
   *
   * @param widgetType Widget type exactly as named in the portal (case-sensitive)
   *
   * Example:
   *   iot.publish("Progress Bar", "V8", 63);
   */
  void publish(const char* widgetType, const char* pin, const TLValue& value);

  // ── Subscribe (app → device) ────────────────────────────────────────────────

  /**
   * Register a callback for Button presses from the app.
   *
   * Example:
   *   iot.onButton("V0", [](bool pressed) {
   *     digitalWrite(LED_PIN, pressed ? HIGH : LOW);
   *   });
   */
  void onButton(const char* pin, void (*callback)(bool));

  /**
   * Register a callback for LED widget taps from the app (on/off).
   *
   * Example:
   *   iot.onLED("V5", [](bool on) {
   *     digitalWrite(LED_PIN, on ? HIGH : LOW);
   *   });
   */
  void onLED(const char* pin, void (*callback)(bool));

  /**
   * Register a callback for Switch toggles from the app.
   *
   * Example:
   *   iot.onSwitch("V1", [](bool on) {
   *     digitalWrite(RELAY_PIN, on ? HIGH : LOW);
   *   });
   */
  void onSwitch(const char* pin, void (*callback)(bool));

  /**
   * Register a callback for Slider values from the app.
   *
   * Example:
   *   iot.onSlider("V2", [](float pct) {
   *     ledcWrite(PWM_CHANNEL, (int)(pct * 2.55f));
   *   });
   */
  void onSlider(const char* pin, void (*callback)(float));

  /**
   * Register a callback for any numeric value from the app.
   *
   * Example:
   *   iot.onValue("V3", [](float v) { Serial.println(v); });
   */
  void onValue(const char* pin, void (*callback)(float));

  /**
   * Register a callback for text typed into a Terminal widget in the app.
   * The pointer is only valid during the callback — copy it to keep it.
   *
   * The apps publish with the MQTT retain flag, so the last text sent is
   * delivered again every time the device reconnects. Do not trigger one-off
   * actions such as a restart directly from it.
   *
   * Example:
   *   iot.onTerminal("V5", [](const char* text) {
   *     Serial.printf("Terminal: %s\n", text);
   *   });
   */
  void onTerminal(const char* pin, void (*callback)(const char* text));

  /**
   * Register a callback for ANY widget type that sends a number from the app.
   * Use this for widget types without a dedicated on...() method, such as a
   * Selector, or types added to the platform later.
   *
   * @param widgetType Widget type exactly as named in the portal (case-sensitive)
   *
   * Example:
   *   iot.onWidget("Selector", "V8", [](float option) {
   *     setMode((int)option);
   *   });
   */
  void onWidget(const char* widgetType, const char* pin, void (*callback)(float));

  /**
   * Same as onWidget(), for widgets that send text (for example a Selector
   * whose options have text values). The pointer is only valid during the
   * callback — copy it to keep it.
   *
   * Example:
   *   iot.onWidgetText("Selector", "V8", [](const char* option) {
   *     if (strcmp(option, "eco") == 0) enterEcoMode();
   *   });
   */
  void onWidgetText(const char* widgetType, const char* pin, void (*callback)(const char* text));

  /**
   * Register a callback for a Selector widget whose options have NUMBER
   * values. Called with the value of the option picked in the app.
   *
   * The device hears its own messages on a pin it listens to, so calling
   * selector() for the same pin runs this callback again with that value.
   * Do not call selector() from inside the callback.
   *
   * Example:
   *   iot.onSelector("V9", [](float option) {
   *     setFanSpeed((int)option);
   *   });
   */
  void onSelector(const char* pin, void (*callback)(float));

  /**
   * Same as onSelector(), for a Selector whose options have TEXT values.
   * The pointer is only valid during the callback — copy it to keep it.
   *
   * Example:
   *   iot.onSelectorText("V9", [](const char* option) {
   *     if (strcmp(option, "eco") == 0) enterEcoMode();
   *   });
   */
  void onSelectorText(const char* pin, void (*callback)(const char* text));

  /**
   * Register a callback for a Number Input widget: the number typed or
   * stepped to in the app. The widget keeps it within its own min and max.
   *
   * Example:
   *   iot.onNumberInput("V10", [](float setpoint) {
   *     targetTemperature = setpoint;
   *   });
   */
  void onNumberInput(const char* pin, void (*callback)(float));

  /**
   * Register a callback for a Text Input widget: the line of text typed in
   * the app. The pointer is only valid during the callback — copy it to keep it.
   *
   * Each line is delivered once. It is not repeated when the device reconnects.
   *
   * Example:
   *   iot.onTextInput("V11", [](const char* text) {
   *     showOnDisplay(text);
   *   });
   */
  void onTextInput(const char* pin, void (*callback)(const char* text));

  /**
   * Register a callback for a Keypad widget whose buttons have NUMBER values.
   * Called with the value of the button pressed; pressing the same button
   * again calls it again.
   *
   * Each press is delivered once. It is not repeated when the device
   * reconnects, so a keypad suits one-off commands.
   *
   * Example:
   *   iot.onKeypad("V12", [](float key) {
   *     runPreset((int)key);
   *   });
   */
  void onKeypad(const char* pin, void (*callback)(float));

  /**
   * Same as onKeypad(), for a Keypad whose buttons have TEXT values.
   * The pointer is only valid during the callback — copy it to keep it.
   *
   * Example:
   *   iot.onKeypadText("V12", [](const char* key) {
   *     if (strcmp(key, "stop") == 0) stopMotor();
   *   });
   */
  void onKeypadText(const char* pin, void (*callback)(const char* text));

  /**
   * Register a callback for RGB widget color changes from the app.
   * Receives individual r, g, b channels (0–255), on/off state,
   * LED count (number of pixels set in the app, 1–300), and
   * pattern name (e.g. "Solid", "Blink" — "" if not sent).
   *
   * Full payload: {"v":1,"r":0,"g":255,"b":204,"status":"ON","count":50,"pattern":"Solid","t":...}
   *
   * Example:
   *   iot.onRGB("V6", [](uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern) {
   *     strip.clear();
   *     if (on) strip.fill(strip.Color(r, g, b), 0, count);
   *     strip.show();
   *     Serial.printf("Pattern: %s\n", pattern);
   *   });
   */
  void onRGB(const char* pin, void (*callback)(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern));

  /**
   * Register a callback for Timer widget values from the app.
   *
   * Example:
   *   iot.onTimer("V10", [](float seconds) {
   *     Serial.println("Timer: " + String((int)seconds) + "s");
   *   });
   */
  void onTimer(const char* pin, void (*callback)(float));

  /**
   * Register a callback for Joystick widget X/Y values from the app.
   * x and y are floats (typically -1.0 to 1.0).
   *
   * Example:
   *   iot.onJoystick("V13", [](float x, float y) {
   *     Serial.printf("X:%.2f  Y:%.2f\n", x, y);
   *   });
   */
  void onJoystick(const char* pin, void (*callback)(float x, float y));

  // ── Status ──────────────────────────────────────────────────────────────────

  bool   wifiConnected();   ///< true if WiFi is up
  bool   mqttConnected();   ///< true if MQTT is connected
  bool   bleActive();       ///< true if BLE provisioning is running
  String getIP();           ///< Current WiFi IP address
  String getChipID();       ///< ESP32 MAC-derived chip identifier

  // ── Offline storage ─────────────────────────────────────────────────────────

  /**
   * Keep readings while there is no connection, and send them when it is back.
   *
   *   iot.offlineStorage(true);    // keep readings while offline
   *   iot.offlineStorage(false);   // default: readings taken offline are dropped
   *
   * With it on, every value the sketch publishes while WiFi is down (or
   * the server cannot be reached) is stored instead of lost. After the device
   * reconnects, the stored readings are sent first, oldest first, each with
   * the time it was taken, so charts and history show them where they belong.
   *
   * Readings are held in memory: they survive a connection loss of any
   * length, but not a restart or a power cut. When the store is full the
   * oldest reading is dropped to make room for the newest.
   *
   * Can be called before or after begin(). Switching it off discards whatever
   * is still waiting.
   *
   * @param enable      true to store readings while offline
   * @param maxRecords  how many readings to keep (default 200, about 20 KB of RAM)
   */
  void offlineStorage(bool enable, uint16_t maxRecords = TL_OFFLINE_DEFAULT_RECORDS);

  /** Number of stored readings still waiting to be sent. */
  uint16_t offlineCount();

  // ── Persistent storage ──────────────────────────────────────────────────────
  // Key strings max 15 characters (ESP32 NVS limit).

  bool   saveString(const char* key, const String& value);
  String getString (const char* key, const String& defaultValue = "");

  bool saveInt(const char* key, int value);
  int  getInt (const char* key, int defaultValue = 0);

  bool  saveFloat(const char* key, float value);
  float getFloat (const char* key, float defaultValue = 0.0f);

  bool saveBool(const char* key, bool value);
  bool getBool (const char* key, bool defaultValue = false);

  bool hasKey   (const char* key);   ///< true if key exists
  bool removeKey(const char* key);   ///< Delete a single key
  void clearAllData();               ///< Erase all app data (not WiFi credentials)

  // ── OTA firmware updates ────────────────────────────────────────────────────

  /**
   * Check for a pending OTA firmware update and apply it if available.
   *
   * WiFi must be connected. On success the device restarts automatically.
   * Call this periodically (e.g. every 60 s) inside loop() after iot.run().
   *
   * @param apiServer  Backend base URL, no trailing slash.
   *                   Defaults to TL_API_SERVER defined in TL_Config.h.
   *                   Override per-call: iot.checkOTA("http://192.168.1.10:8000")
   * @return OTAResult  (OTA_NO_UPDATE / OTA_SUCCESS / OTA_FAILED / OTA_ERROR)
   *
   * Example:
   *   if (iot.wifiConnected()) {
   *     OTAResult r = iot.checkOTA();
   *     if (r == OTA_FAILED) Serial.println("OTA failed — will retry.");
   *   }
   */
  OTAResult checkOTA(const char* apiServer = TL_API_SERVER);

  /**
   * Install OTA updates automatically, without checking every minute.
   *
   * The server rings an MQTT doorbell when this device has an update; run()
   * then asks the API (HTTPS) and installs it. As a safety net run() also
   * checks every TL_OTA_SAFETY_CHECK_MS (12 h, plus a random spread) and once
   * a few minutes after start-up. Call once in setup(), after begin().
   *
   * Example:
   *   iot.begin();
   *   iot.enableOTA();
   */
  void enableOTA(const char* apiServer = TL_API_SERVER);

  // ── Advanced ────────────────────────────────────────────────────────────────

  /**
   * Set a custom brand name for BLE advertising.
   * The device will appear as "<brandName>_<ChipID>" in BLE scans.
   * Call this BEFORE begin().
   *
   * Default: "ThingsLinker"
   *
   * Example:
   *   iot.setBLEName("AcmeSensors");
   *   // Device appears as: AcmeSensors_BC6575C55494
   *   iot.begin();
   */
  void setBLEName(const char* brandName);

  /**
   * Clear the saved WiFi network and go into Bluetooth setup mode.
   * Useful for a factory-reset button or changing networks.
   *
   * If WiFi was in use, the device restarts (setup mode then begins on a clean
   * boot, which is what makes the new network connect reliably), so code after
   * this call does not run in that case.
   *
   * The same reset also happens when the user holds the reset button (see
   * resetButton()) or chooses Reset WiFi in the app or the User Portal (see
   * remoteReset()).
   */
  void resetWiFi();

  /**
   * Use a button on the device to reset its WiFi: holding it for holdMs
   * calls resetWiFi(). run() checks the button, so nothing else is needed.
   *
   * @param pin        GPIO of the button (default 0, the BOOT button on most ESP32 boards)
   * @param holdMs     how long it must be held, in ms (default 3000)
   * @param activeLow  true when pressing connects the pin to GND (the usual
   *                   wiring; the internal pull-up is turned on)
   *
   * Example:
   *   iot.resetButton();        // hold BOOT (GPIO 0) for 3 s
   *   iot.resetButton(41);      // a button on GPIO 41
   *   iot.begin();
   */
  void resetButton(uint8_t pin = 0, uint16_t holdMs = TL_RESET_HOLD_MS, bool activeLow = true);

  /**
   * Allow or refuse Reset WiFi from the app and the User Portal.
   * On by default. Call before begin().
   *
   * Example:
   *   iot.remoteReset(false);   // only the button on the device can reset WiFi
   */
  void remoteReset(bool enable);

  /**
   * Enable or disable library debug output on Serial.
   * Debug is enabled by default. Disabling reduces Serial traffic.
   *
   * Example:
   *   iot.debug(false);  // Silent mode
   */
  void debug(bool enable);

private:
  const char*   _authToken;
  const char*   _blueprintId;
  const char*   _mqttUsername;
  const char*   _mqttPassword;
  const char*   _bleBrandName;   // Custom BLE prefix (default: "ThingsLinker")
  bool          _initialized;
  unsigned long _lastCheck;
  bool          _otaEnabled = false;
  const char*   _otaServer  = nullptr;
  unsigned long _otaNextAt  = 0;     // millis() of the next safety-net check

  int16_t       _resetPin;        // -1 = no reset button
  uint16_t      _resetHoldMs;
  bool          _resetActiveLow;
  unsigned long _resetPressedAt;  // 0 = not pressed
  bool          _remoteReset;

  void handleBLEProvisioning();
  void serviceOTA();                 ///< OTA doorbell + safety-net check (see enableOTA)
  void checkConnections();
  void checkResetButton();
};

#endif // THINGSLINKER_H
