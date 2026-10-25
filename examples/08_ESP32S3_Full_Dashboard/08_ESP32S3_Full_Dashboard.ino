/**
 * ============================================================
 *   ThingsLinker — Full Dashboard (ESP32-S3)
 * ============================================================
 *
 *  Matches this exact portal widget configuration:
 *
 *  ┌─────┬────────────────┬────────────┬──────────────────────────────┐
 *  │ Pin │ Widget         │ Direction  │ Description                  │
 *  ├─────┼────────────────┼────────────┼──────────────────────────────┤
 *  │ V0  │ Button         │ App↔Device │ Room Light1 (GPIO41 button)  │
 *  │ V1  │ Chart (°C)     │ Device→App │ Temperature Room             │
 *  │ V2  │ Chart (°F)     │ Device→App │ Humidity                     │
 *  │ V3  │ Gauge (°)      │ Device→App │ Generic gauge                │
 *  │ V4  │ Label          │ Device→App │ Pressure hPa                 │
 *  │ V5  │ LED            │ App↔Device │ RGB strip on/off (tap+mirror)│
 *  │ V6  │ RGB            │ App→Device │ Color picker → GPIO4 strip   │
 *  │ V7  │ Slider         │ App→Device │ Brightness 0–100             │
 *  │ V8  │ Switch         │ App→Device │ RGB strip master on/off      │
 *  │ V9  │ Terminal       │ Device→App │ Uptime log every 5 s         │
 *  │ V10 │ Timer          │ App→Device │ Countdown value (seconds)    │
 *  │ V11 │ Value Display  │ Device→App │ CO₂ ppm (simulated)          │
 *  │ V12 │ Map            │ Device→App │ GPS location (static demo)   │
 *  │ V13 │ Joystick       │ App→Device │ X/Y direction values         │
 *  └─────┴────────────────┴────────────┴──────────────────────────────┘
 *
 *  Hardware:
 *    GPIO 4  — WS2812 NeoPixel RGB LED strip data pin
 *    GPIO 41 — Push button (Room Light1 / BOOT button on some boards)
 *
 *  RGB strip control:
 *    V5  LED widget    → mirrors current on/off state back to the app
 *    V6  RGB widget    → app sets color; strip updates instantly
 *    V7  Slider widget → app sets brightness 0–100 %
 *    V8  Switch widget → app master on/off for the strip
 *
 *  Dependencies (install via Arduino Library Manager):
 *    - Adafruit NeoPixel   (for WS2812 / WS2811 LED strips)
 *    - ArduinoJson         (already required by ThingsLinker)
 *    - PubSubClient        (already required by ThingsLinker)
 *
 *  BLE Onboarding (automatic):
 *    First boot → BLE starts as "ThingsLinker_<ChipID>"
 *    Open ThingsLinker app → Add Device → enter WiFi credentials
 *    Subsequent boots → auto-connects (no BLE needed)
 *    Hold GPIO41 > 3 s → clears WiFi, restarts BLE
 *
 *  CREDENTIALS: Blueprints → [Blueprint] → Devices → [Device]
 * ============================================================
 */

#include <ThingsLinker.h>
#include <Adafruit_NeoPixel.h>   // Install: Arduino Library Manager → "Adafruit NeoPixel"
#include "RGBEffects.h"          // colour correction and the RGB widget's effects (this folder)

// ── Credentials ──────────────────────────────────────────────
const char* AUTH_TOKEN   = "9a1977dd443b7d1f0ff4cbdaa4cbf05e1376b786dcf1da344d06a0d8df46845f";
const char* BLUEPRINT_ID = "BLUEbV7N2u5diuUi";

// ── Hardware ─────────────────────────────────────────────────
#define BUTTON_PIN     41   // Physical push button (Room Light1)
#define RGB_PIN         4   // WS2812 NeoPixel data pin
#define RGB_LED_COUNT   8   // Number of LEDs in your strip — change as needed

// ── Safety brightness cap ────────────────────────────────────
// WS2812 draws ~60 mA per LED at full white (R+G+B all 255).
// 8 LEDs × 60 mA = 480 mA — too much for USB-only power and
// triggers the ESP32-S3 brownout detector (~3.1 V threshold).
//
// MAX_BRIGHTNESS caps the strip to ~18 mA/LED (8 LEDs ≈ 144 mA),
// which is safe on USB 500 mA shared with the ESP32 (~100 mA).
//
// To run at full brightness, power the strip from an external
// 5 V / 2 A supply and share GND with the ESP32, then set
// MAX_BRIGHTNESS to 100.
#define MAX_BRIGHTNESS  30  // 0–100 %; raise only with external 5 V power

// ── Colour order ─────────────────────────────────────────────
// Most WS2812 strips are GRB. If red shows as green (and green as
// red), change this to NEO_RGB.
#define RGB_COLOR_ORDER NEO_GRB

// ── NeoPixel strip ───────────────────────────────────────────
Adafruit_NeoPixel strip(RGB_LED_COUNT, RGB_PIN, RGB_COLOR_ORDER + NEO_KHZ800);
RGBEffects        effects(strip);

// ── ThingsLinker ─────────────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// ── RGB strip state ───────────────────────────────────────────
bool    rgbOn     = false;
uint8_t rgbR      = 255;   // Default color: white
uint8_t rgbG      = 255;
uint8_t rgbB      = 255;
float    brightness  = 50.0f;  // 0–100 %
uint16_t pixelCount  = RGB_LED_COUNT; // number of LEDs to light (set from app RGB widget)
String   rgbPattern  = "Default";     // effect picked in the RGB widget

// ── App → Device state ────────────────────────────────────────
float timerSecs  = 0.0f;
float joystickX  = 0.0f;
float joystickY  = 0.0f;

// ── Simulated sensor values ───────────────────────────────────
float temperature = 25.0f;   // V1  Chart  °C
float humidity    = 60.0f;   // V2  Chart  (°F unit in portal)
float gaugeValue  = 50.0f;   // V3  Gauge  °
float pressure    = 1013.0f; // V4  Label  hPa
float co2         = 800.0f;  // V11 Value Display  ppm

// ── Static GPS for Map widget ─────────────────────────────────
const float MAP_LAT = 23.0225f;  // Ahmedabad, India — change to your location
const float MAP_LNG = 72.5714f;

// ── Button debounce ───────────────────────────────────────────
bool          btnLastState   = HIGH;
bool          roomLightOn    = false;
bool          btnHoldActive  = false;
unsigned long btnHoldStart   = 0;

// ── Timing ───────────────────────────────────────────────────
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 5000;

// ── Connection tracking (for reconnect state sync) ────────────
bool _prevMQTTConnected = false;

// ─────────────────────────────────────────────────────────────
// Apply current RGB state (color + brightness) to the strip.
//
// publishLed = true  → also publish LED widget state to the app
//              false → strip only, no MQTT publish
//
// Pass false when called from onLEDToggle to prevent the self-
// echo loop: device publishes V5 → receives its own message →
// publishes again → infinite rapid loop.
// ─────────────────────────────────────────────────────────────
void applyRGB(bool publishLed = true) {
  // RGBEffects draws the strip from loop(): colour (gamma-corrected so it
  // matches the app), LED count, effect, and brightness capped for USB power.
  float capped = brightness > MAX_BRIGHTNESS ? MAX_BRIGHTNESS : brightness;
  effects.set(rgbR, rgbG, rgbB, rgbOn, pixelCount, rgbPattern.c_str(), capped);

  // V5 — LED widget: mirror on/off state back to the app
  if (publishLed) {
    iot.led("V5", rgbOn);
  }
}

// ── Helper: smooth drift within [minV, maxV] ─────────────────
float drift(float v, float minV, float maxV, float step) {
  float delta = ((float)(random(-100, 101))) / 100.0f * step;
  v += delta;
  if (v < minV) v = minV;
  if (v > maxV) v = maxV;
  return v;
}

// ─────────────────────────────────────────────────────────────
// V0: Button — Room Light1
//   App button → printed to Serial (no physical LED in this demo)
//   Physical GPIO41 press → published to V0 Button widget
// ─────────────────────────────────────────────────────────────
void onRoomLight(bool pressed) {
  roomLightOn = pressed;
  iot.saveBool("room_light", pressed);
  Serial.println("[V0 Button] Room Light1: " + String(pressed ? "ON" : "OFF"));
  // Add: digitalWrite(LIGHT_PIN, pressed ? HIGH : LOW);
}

// ─────────────────────────────────────────────────────────────
// V5: LED — Strip on/off (bidirectional)
//   Device→App : applyRGB() publishes current on/off state so
//                the LED widget in the app always mirrors the strip.
//   App→Device : tapping the LED widget in the app sends on/off
//                here, just like the Switch (V8).
//   Both V5 LED and V8 Switch control the same strip — whichever
//   is used last wins.
// ─────────────────────────────────────────────────────────────
void onLEDToggle(bool on) {
  rgbOn = on;
  iot.saveBool("rgb_on", rgbOn);
  applyRGB(false);  // false = don't publish LED state back — prevents self-echo loop
  Serial.println("[V5 LED] Strip: " + String(rgbOn ? "ON" : "OFF"));
}

// ─────────────────────────────────────────────────────────────
// V6: RGB — Color picker
//
//  ThingsLinker RGB widget payload:
//    {"v":1,"r":0,"g":255,"b":204,"status":"ON","count":50,"pattern":"Solid","t":ts}
//    v       = 1 ON / 0 OFF
//    r, g, b = color channels 0–255
//    status  = "ON" / "OFF"
//    count   = number of LEDs to light (1–300)
//    pattern = effect name from the widget's Effect list, e.g.
//              "Default", "Blinking", "Rainbow Cycle", "Fire Effect"
//
//  iot.onRGB() subscribes to the "RGB" topic and decodes all fields.
// ─────────────────────────────────────────────────────────────
void onRGBColor(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern) {
  rgbR       = r;
  rgbG       = g;
  rgbB       = b;
  rgbOn      = on;
  pixelCount = count;
  rgbPattern = pattern ? pattern : "Default";
  iot.saveInt ("rgb_r",   r);
  iot.saveInt ("rgb_g",   g);
  iot.saveInt ("rgb_b",   b);
  iot.saveBool("rgb_on",  on);
  iot.saveInt ("rgb_cnt", count);
  iot.saveString("rgb_pat", rgbPattern);
  applyRGB();
  Serial.printf("[V6 RGB] R:%d  G:%d  B:%d  Strip:%s  Count:%d  Pattern:%s\n",
                rgbR, rgbG, rgbB, rgbOn ? "ON" : "OFF", pixelCount, pattern);
}

// ─────────────────────────────────────────────────────────────
// V7: Slider — Brightness (0–100 %)
// ─────────────────────────────────────────────────────────────
void onBrightness(float value) {
  brightness = value;
  iot.saveFloat("brightness", value);
  applyRGB();
  Serial.println("[V7 Slider] Brightness: " + String((int)brightness) + "%");
}

// ─────────────────────────────────────────────────────────────
// V8: Switch — RGB strip master on/off
// ─────────────────────────────────────────────────────────────
void onRGBSwitch(bool on) {
  rgbOn = on;
  iot.saveBool("rgb_on", on);
  applyRGB();
  Serial.println("[V8 Switch] RGB Strip: " + String(rgbOn ? "ON" : "OFF"));
}

// ─────────────────────────────────────────────────────────────
// V10: Timer — countdown value from app
// ─────────────────────────────────────────────────────────────
void onTimer(float value) {
  timerSecs = value;
  iot.saveFloat("timer_secs", value);
  Serial.println("[V10 Timer] Remaining: " + String((int)timerSecs) + " s");
}

// ─────────────────────────────────────────────────────────────
// V13: Joystick — X/Y axis from app
//   ThingsLinker Joystick payload: {"v":0,"x":0.5,"y":-0.3,"t":ts}
//   iot.onJoystick() subscribes to the correct "Joystick" topic
//   and passes x, y directly — no manual decoding needed.
// ─────────────────────────────────────────────────────────────
void onJoystick(float x, float y) {
  joystickX = x;
  joystickY = y;
  Serial.printf("[V13 Joystick] X:%.2f  Y:%.2f\n", joystickX, joystickY);
}

// ─────────────────────────────────────────────────────────────
// Publish all sensor + telemetry data to the app
// ─────────────────────────────────────────────────────────────
void sendSensorData() {
  // Drift simulated values
  temperature = drift(temperature, 20.0f, 35.0f, 0.5f);
  humidity    = drift(humidity,    40.0f, 80.0f, 1.0f);
  gaugeValue  = drift(gaugeValue,   0.0f, 100.0f, 2.0f);
  pressure    = drift(pressure,  1010.0f, 1020.0f, 0.3f);
  co2         = drift(co2,        400.0f, 2000.0f, 20.0f);

  // V1 — Chart: Temperature °C
  iot.chart("V1", temperature);

  // V2 — Chart: Humidity (portal label shows °F)
  iot.chart("V2", humidity);

  // V3 — Gauge: generic value (°)
  iot.gauge("V3", gaugeValue);

  // V4 — Label: Pressure hPa
  iot.label("V4", pressure);

  // V5 — LED: RGB strip on/off state (also updated immediately in applyRGB())
  iot.led("V5", rgbOn);

  // V9 — Terminal: uptime log line
  iot.terminal("V9", String("Uptime ") + String(millis() / 1000UL) + " s");

  // V11 — Value Display: CO₂ ppm
  iot.display("V11", co2);

  // V12 — Map: publish latitude + longitude
  iot.map("V12", MAP_LAT, MAP_LNG);

  Serial.println("─────────────────────────────────────────────");
  Serial.printf("[Sensor] Temp:%.1f°C  Hum:%.0f%%  Gauge:%.1f°  Pres:%.1fhPa  CO2:%.0fppm\n",
                temperature, humidity, gaugeValue, pressure, co2);
  Serial.println("─────────────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────
// Handle physical push button on GPIO41
//   Short press (<3 s) → toggle Room Light1, publish V0
//   Long press  (≥3 s) → reset WiFi credentials, restart BLE
// ─────────────────────────────────────────────────────────────
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);

  // Detect press (falling edge)
  if (reading == LOW && btnLastState == HIGH) {
    btnHoldStart  = millis();
    btnHoldActive = true;
  }

  // Detect release (rising edge)
  if (reading == HIGH && btnLastState == LOW) {
    unsigned long held = millis() - btnHoldStart;
    btnHoldActive = false;

    if (held >= 3000) {
      // Long press — reset WiFi
      Serial.println("\n[BOOT] Long press — clearing WiFi, restarting BLE...");
      iot.resetWiFi();
    } else {
      // Short press — toggle Room Light1
      roomLightOn = !roomLightOn;
      iot.saveBool("room_light", roomLightOn);
      iot.button("V0", roomLightOn);
      Serial.println("[GPIO41] Room Light1: " + String(roomLightOn ? "ON" : "OFF"));
    }
  }

  btnLastState = reading;
}

// ─────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║  ThingsLinker — Full Dashboard (ESP32-S3)║");
  Serial.println("╚══════════════════════════════════════════╝");

  // Hardware init
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // NeoPixel init — strip off at startup
  strip.begin();
  strip.clear();
  strip.show();

  // Boot blink: 3× blue pulses confirm power-on
  for (int i = 0; i < 3; i++) {
    strip.fill(strip.Color(0, 0, 80));
    strip.show();
    delay(150);
    strip.clear();
    strip.show();
    delay(150);
  }

  // ── Start ThingsLinker ──────────────────────────────────────
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // ── Restore last state from NVS (survives reboot) ───────────
  // Two-layer state recovery:
  //   1. NVS (here, immediate) — restores the last saved state from flash.
  //      Works even if the broker was restarted and has no retained messages.
  //   2. MQTT retained (fires via callbacks after subscriptions below) —
  //      broker delivers the last app command for every subscribed topic,
  //      overwriting NVS values with the most recent user intent.
  //
  // All App→Device widgets are covered:
  //   V0  Button  → roomLightOn
  //   V5  LED     → rgbOn
  //   V6  RGB     → rgbR, rgbG, rgbB, rgbOn, pixelCount, rgbPattern
  //   V7  Slider  → brightness
  //   V8  Switch  → rgbOn
  //   V10 Timer   → timerSecs
  roomLightOn =          iot.getBool ("room_light", false);
  rgbR        = (uint8_t)iot.getInt  ("rgb_r",      255);
  rgbG        = (uint8_t)iot.getInt  ("rgb_g",      255);
  rgbB        = (uint8_t)iot.getInt  ("rgb_b",      255);
  rgbOn       =           iot.getBool ("rgb_on",      false);
  pixelCount  = (uint16_t)iot.getInt  ("rgb_cnt",     RGB_LED_COUNT);
  rgbPattern  =           iot.getString("rgb_pat",    "Default");
  brightness  =           iot.getFloat("brightness",  50.0f);
  timerSecs   =           iot.getFloat("timer_secs",  0.0f);
  applyRGB();  // apply restored RGB state to strip immediately
  Serial.printf("[NVS] Room:%s  RGB:%s  R:%d G:%d B:%d  Count:%d  Brightness:%.0f%%  Timer:%.0fs\n",
                roomLightOn ? "ON" : "OFF",
                rgbOn       ? "ON" : "OFF",
                rgbR, rgbG, rgbB, pixelCount, brightness, timerSecs);

  // ── App → Device subscriptions ──────────────────────────────
  iot.onButton  ("V0",  onRoomLight);  // Button   — Room Light1
  iot.onLED     ("V5",  onLEDToggle);  // LED      — strip on/off tap from app
  iot.onRGB     ("V6",  onRGBColor);   // RGB      — Color picker (device/RGB/...)
  iot.onSlider  ("V7",  onBrightness); // Slider   — Brightness
  iot.onSwitch  ("V8",  onRGBSwitch);  // Switch   — RGB on/off
  iot.onTimer   ("V10", onTimer);      // Timer    — Countdown (device/Timer/...)
  iot.onJoystick("V13", onJoystick);   // Joystick — X/Y (device/Joystick/...)

  // ── Widget map ──────────────────────────────────────────────
  Serial.println("\n[Widget Map]");
  Serial.println("  V0   Button         App↔Device  Room Light1 (GPIO41)");
  Serial.println("  V1   Chart (°C)     Device→App  Temperature");
  Serial.println("  V2   Chart (°F)     Device→App  Humidity");
  Serial.println("  V3   Gauge (°)      Device→App  Gauge");
  Serial.println("  V4   Label          Device→App  Pressure");
  Serial.println("  V5   LED            App↔Device  RGB strip on/off (tap = toggle)");
  Serial.println("  V6   RGB            App→Device  Color (GPIO4)");
  Serial.println("  V7   Slider         App→Device  Brightness 0–100%");
  Serial.println("  V8   Switch         App→Device  RGB on/off");
  Serial.println("  V9   Terminal       Device→App  Uptime log");
  Serial.println("  V10  Timer          App→Device  Countdown");
  Serial.println("  V11  Value Display  Device→App  CO₂ ppm");
  Serial.println("  V12  Map            Device→App  GPS");
  Serial.println("  V13  Joystick       App→Device  X/Y");
  Serial.println();
  Serial.println("  RGB strip : GPIO4   (" + String(RGB_LED_COUNT) + " LEDs)");
  Serial.println("  Button    : GPIO41  (short press = toggle, long >3s = reset WiFi)");
  Serial.println("───────────────────────────────────────────\n");

  // Push current state immediately so the app shows correct values as soon
  // as the device comes online — don't wait for the first 5-second interval.
  if (iot.mqttConnected()) {
    sendSensorData();
    _prevMQTTConnected = true;
  }
}

// ─────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  // Must be called every iteration — drives WiFi, MQTT, BLE, reconnect
  iot.run();

  bool nowConnected = iot.mqttConnected();

  // Detect fresh MQTT connection (first connect or after drop/reconnect).
  // Push current state immediately so the app reflects correct values
  // the moment the device comes back online — don't wait up to 5 seconds.
  if (nowConnected && !_prevMQTTConnected) {
    Serial.println("[Reconnect] MQTT connected — pushing current state…");
    sendSensorData();
    lastPublish = millis();  // reset interval so next publish is a full 5 s away
  }
  _prevMQTTConnected = nowConnected;

  // Publish sensor data on interval
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
    if (nowConnected) {
      sendSensorData();
    }
  }

  // Draw the RGB strip (animates the selected effect)
  effects.loop();

  // Poll physical button
  handleButton();

  delay(10);
}