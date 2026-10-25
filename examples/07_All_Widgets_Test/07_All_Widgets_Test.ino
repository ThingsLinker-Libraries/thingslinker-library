/**
 * ============================================================
 *   ThingsLinker — All Widgets Test
 * ============================================================
 *
 *  Tests every widget type supported by the ThingsLinker app.
 *  Uses simulated sensor values — no real hardware needed
 *  except the built-in LED and BOOT button.
 *
 *  Widget pin map:
 *  ┌─────┬──────────────┬────────────┬──────────────────────────────┐
 *  │ Pin │ Widget       │ Direction  │ Description                  │
 *  ├─────┼──────────────┼────────────┼──────────────────────────────┤
 *  │ V0  │ Button       │ App→Device │ Toggles built-in LED         │
 *  │ V1  │ Switch       │ App→Device │ Relay sim (prints to Serial) │
 *  │ V2  │ Slider       │ App→Device │ RGB strip brightness 0–100   │
 *  │ V3  │ RGB          │ App→Device │ Color, on/off, count, effect │
 *  │ V4  │ Timer        │ App→Device │ Countdown seconds            │
 *  │ V5  │ Joystick     │ App→Device │ X/Y axis –1.0 to 1.0        │
 *  │ V6  │ Gauge        │ Device→App │ Temperature °C               │
 *  │ V7  │ Chart        │ Device→App │ Temperature history          │
 *  │ V8  │ Value Display│ Device→App │ Humidity %                   │
 *  │ V9  │ Label        │ Device→App │ Pressure hPa                 │
 *  │ V10 │ LED          │ App↔Device │ Mirrors built-in LED state   │
 *  └─────┴──────────────┴────────────┴──────────────────────────────┘
 *
 *  Hardware: any ESP32 board.
 *    Built-in LED: GPIO 2 (change LED_PIN if different)
 *    BOOT button:  GPIO 0 (hold >3 s to reset WiFi)
 *    RGB strip:    WS2812 / NeoPixel data on GPIO 4 (RGB_PIN), shown
 *                  by the V3 RGB widget. Needs the "Adafruit NeoPixel"
 *                  library (Arduino Library Manager).
 *
 *  CREDENTIALS: ThingsLinker org portal →
 *    Blueprints → [Blueprint] → Devices → [Device]
 * ============================================================
 */

#include <ThingsLinker.h>
#include <Adafruit_NeoPixel.h>   // Install: Arduino Library Manager → "Adafruit NeoPixel"
#include "RGBEffects.h"          // colour correction and the RGB widget's effects (this folder)

// ── Credentials ──────────────────────────────────────────────
const char* AUTH_TOKEN   = "YOUR_AUTH_TOKEN";
const char* BLUEPRINT_ID = "YOUR_BLUEPRINT_ID";

// ── Hardware ─────────────────────────────────────────────────
#define LED_PIN   2   // Built-in LED on most ESP32 boards
#define BOOT_PIN  0   // BOOT button — hold >3 s to reset WiFi

// ── RGB strip (V3) ───────────────────────────────────────────
// To use the single RGB LED built into an ESP32-S3 DevKit instead,
// set RGB_PIN to 48 (38 on some boards) and RGB_LED_COUNT to 1.
#define RGB_PIN          4    // WS2812 / NeoPixel data pin
#define RGB_LED_COUNT    8    // LEDs on the strip
#define RGB_COLOR_ORDER  NEO_GRB  // if red shows as green, use NEO_RGB
#define MAX_BRIGHTNESS   30   // 0–100 %; keeps USB power safe, raise with a 5 V supply

Adafruit_NeoPixel strip(RGB_LED_COUNT, RGB_PIN, RGB_COLOR_ORDER + NEO_KHZ800);
RGBEffects        effects(strip);

// ── ThingsLinker ─────────────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// ── App → Device state ───────────────────────────────────────
bool  ledOn       = false;
bool  relayOn     = false;
float brightness  = 50.0f;
uint8_t rgbR = 255, rgbG = 255, rgbB = 255;
bool    rgbOn     = false;
uint16_t rgbCount  = RGB_LED_COUNT;
String   rgbPattern = "Default";
float   timerSecs = 0.0f;
float   joystickX = 0.0f, joystickY = 0.0f;

// ── Simulated sensor values ───────────────────────────────────
float temperature = 27.5f;
float humidity    = 60.0f;
float pressure    = 1013.0f;

// ── Timing ───────────────────────────────────────────────────
unsigned long lastPublish     = 0;
bool          _prevMQTT       = false;

const unsigned long PUBLISH_INTERVAL = 5000;

// ── Helper: smooth random drift ──────────────────────────────
float drift(float v, float lo, float hi, float step) {
  v += ((float)(random(-100, 101))) / 100.0f * step;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return v;
}

// Shows the current RGB state on the strip. effects.loop() draws it.
void applyRGB() {
  float capped = brightness > MAX_BRIGHTNESS ? MAX_BRIGHTNESS : brightness;
  effects.set(rgbR, rgbG, rgbB, rgbOn, rgbCount, rgbPattern.c_str(), capped);
}

// ── Callbacks: App → Device ──────────────────────────────────

// V0: Button — toggles built-in LED + mirrors to V10 LED widget
void onButton(bool pressed) {
  ledOn = pressed;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  iot.led("V10", ledOn);  // mirror state to LED widget
  iot.saveBool("led_on", ledOn);
  Serial.println("[V0 Button] LED: " + String(ledOn ? "ON" : "OFF"));
}

// V1: Switch — relay simulation
void onSwitch(bool on) {
  relayOn = on;
  iot.saveBool("relay_on", on);
  Serial.println("[V1 Switch] Relay: " + String(relayOn ? "ON" : "OFF"));
  // digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

// V2: Slider — RGB strip brightness 0–100
void onSlider(float value) {
  brightness = value;
  iot.saveFloat("brightness", value);
  applyRGB();
  Serial.println("[V2 Slider] Brightness: " + String((int)brightness) + "%");
}

// V3: RGB — color picker + on/off + LED count + pattern
void onRGB(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern) {
  rgbR = r; rgbG = g; rgbB = b; rgbOn = on;
  rgbCount   = count;
  rgbPattern = pattern ? pattern : "Default";
  iot.saveInt ("rgb_r",   r);
  iot.saveInt ("rgb_g",   g);
  iot.saveInt ("rgb_b",   b);
  iot.saveBool("rgb_on",  on);
  iot.saveInt ("rgb_cnt", count);
  iot.saveString("rgb_pat", rgbPattern);
  applyRGB();
  Serial.printf("[V3 RGB] R:%d G:%d B:%d  On:%s  Count:%d  Pattern:%s\n",
                r, g, b, on ? "YES" : "NO", count, pattern);
}

// V4: Timer — countdown value from app
void onTimer(float seconds) {
  timerSecs = seconds;
  iot.saveFloat("timer_secs", seconds);
  Serial.println("[V4 Timer] Remaining: " + String((int)timerSecs) + " s");
}

// V5: Joystick — X/Y axis values (–1.0 to 1.0)
void onJoystick(float x, float y) {
  joystickX = x; joystickY = y;
  Serial.printf("[V5 Joystick] X:%.2f  Y:%.2f\n", x, y);
}

// V10: LED — tap from app toggles LED (no echo-back to avoid loop)
void onLED(bool on) {
  ledOn = on;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  iot.saveBool("led_on", on);
  Serial.println("[V10 LED] LED: " + String(ledOn ? "ON" : "OFF"));
  // Note: do NOT call iot.led("V10", ...) here — causes self-echo loop
}

// ── Publish all sensor + status data ─────────────────────────
void publishAll() {
  temperature = drift(temperature, 20.0f, 35.0f, 0.5f);
  humidity    = drift(humidity,    40.0f, 80.0f, 1.0f);
  pressure    = drift(pressure,  1010.0f, 1020.0f, 0.3f);

  iot.gauge  ("V6",  temperature);   // Gauge
  iot.chart  ("V7",  temperature);   // Chart
  iot.display("V8",  humidity);      // Value Display
  iot.label  ("V9",  pressure);      // Label
  iot.led    ("V10", ledOn);         // LED state indicator

  Serial.printf("[Sensor] Temp:%.1f°C  Hum:%.0f%%  Pres:%.1fhPa\n",
                temperature, humidity, pressure);
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println(  "║   ThingsLinker — All Widgets Test        ║");
  Serial.println(  "╚══════════════════════════════════════════╝\n");

  pinMode(LED_PIN,  OUTPUT);
  digitalWrite(LED_PIN, LOW);

  strip.begin();   // RGB strip off at startup
  strip.clear();
  strip.show();

  // Boot blink: 3× confirms power-on
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(150);
    digitalWrite(LED_PIN, LOW);  delay(150);
  }

  // Hold BOOT for 3 s to reset WiFi. Reset WiFi in the app or the User
  // Portal does the same (turn that off with iot.remoteReset(false)).
  iot.resetButton(BOOT_PIN);

  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // ── Restore last state from NVS (survives reboot) ───────────
  ledOn      = iot.getBool ("led_on",    false);
  relayOn    = iot.getBool ("relay_on",  false);
  brightness = iot.getFloat("brightness",50.0f);
  rgbR       = (uint8_t)iot.getInt("rgb_r", 255);
  rgbG       = (uint8_t)iot.getInt("rgb_g", 255);
  rgbB       = (uint8_t)iot.getInt("rgb_b", 255);
  rgbOn      = iot.getBool ("rgb_on",   false);
  rgbCount   = (uint16_t)iot.getInt("rgb_cnt", RGB_LED_COUNT);
  rgbPattern = iot.getString("rgb_pat", "Default");
  timerSecs  = iot.getFloat("timer_secs", 0.0f);
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  applyRGB();
  Serial.printf("[NVS] LED:%s  Relay:%s  RGB:%s  Brightness:%.0f%%\n",
                ledOn ? "ON":"OFF", relayOn ? "ON":"OFF",
                rgbOn ? "ON":"OFF", brightness);

  // ── Subscribe: App → Device ──────────────────────────────────
  iot.onButton  ("V0",  onButton);    // Button
  iot.onSwitch  ("V1",  onSwitch);    // Switch
  iot.onSlider  ("V2",  onSlider);    // Slider
  iot.onRGB     ("V3",  onRGB);       // RGB color picker
  iot.onTimer   ("V4",  onTimer);     // Timer countdown
  iot.onJoystick("V5",  onJoystick);  // Joystick X/Y
  iot.onLED     ("V10", onLED);       // LED tap from app

  // Push initial state immediately on first connect
  if (iot.mqttConnected()) {
    publishAll();
    lastPublish  = millis();
    _prevMQTT    = true;
  }

  Serial.println("\n[Widgets] All registered:");
  Serial.println("  V0  Button       App→Device  Toggle LED");
  Serial.println("  V1  Switch       App→Device  Relay on/off");
  Serial.println("  V2  Slider       App→Device  RGB brightness 0–100");
  Serial.println("  V3  RGB          App→Device  RGB strip on GPIO " + String(RGB_PIN));
  Serial.println("  V4  Timer        App→Device  Countdown");
  Serial.println("  V5  Joystick     App→Device  X/Y axis");
  Serial.println("  V6  Gauge        Device→App  Temperature °C");
  Serial.println("  V7  Chart        Device→App  Temperature history");
  Serial.println("  V8  Value Disp.  Device→App  Humidity %");
  Serial.println("  V9  Label        Device→App  Pressure hPa");
  Serial.println("  V10 LED          App↔Device  LED indicator");
  Serial.println("\n  Hold BOOT >3 s to reset WiFi");
  Serial.println("───────────────────────────────────────────\n");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  iot.run();

  bool now_connected = iot.mqttConnected();

  // Detect reconnect → push state immediately
  if (now_connected && !_prevMQTT) {
    Serial.println("[Reconnect] MQTT connected — pushing state...");
    publishAll();
    lastPublish = millis();
  }
  _prevMQTT = now_connected;

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    if (now_connected) publishAll();
  }

  effects.loop();   // draws the RGB strip and animates its effect
  delay(10);
}
