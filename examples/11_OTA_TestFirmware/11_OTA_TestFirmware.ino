/**
 * ThingsLinker — Example 11: OTA Test Firmware (NeoPixel RGB)
 * ============================================================
 *
 * PURPOSE: Compile this as the "new" .bin to upload in an OTA shipment.
 *          When the device running 08_OTA_Update (v1.0) detects the update,
 *          it flashes this firmware and restarts — you'll see "v2.0" in
 *          Serial, confirming OTA worked. The NeoPixel strip is then
 *          controlled live from the User Portal.
 *
 * Portal setup (User Portal → Devices → [Device] → Add Widget):
 *   V6 → RGB widget    — pick any color, set pattern (Solid / Blink / Fade)
 *   V8 → Switch widget — strip master on / off
 *
 * Hardware:
 *   GPIO 4 → WS2812 / NeoPixel data pin
 *   5V     → NeoPixel 5V (share GND with ESP32)
 *
 * Dependencies (Arduino Library Manager):
 *   - Adafruit NeoPixel  (search "Adafruit NeoPixel")
 *   - ArduinoJson        (already required by ThingsLinker)
 *
 * Safety note:
 *   MAX_BRIGHTNESS is capped at 30 % for USB-powered setups (~144 mA for 8 LEDs).
 *   Raise to 100 only with an external 5V/2A supply.
 *
 * IMPORTANT — Partition Scheme & Bootloader
 * ------------------------------------------
 * This .bin is the "new firmware" you upload to the OTA shipment. It MUST be
 * compiled with the SAME partition scheme as the base firmware (08_OTA_Update).
 * In Arduino IDE set:
 *   Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)
 *   ("Default with OTA" leaves 1.3MB per app, which a WiFi + OTA sketch now
 *   slightly exceeds.)
 *
 * Rules:
 *  - Use the SAME partition scheme for every firmware on this device.
 *  - The bootloader is flashed once via USB and never changed by OTA.
 *  - Compiling this .bin with a different scheme (e.g. "No OTA" or "Huge APP")
 *    will cause the device to crash after the OTA flash — it looks like success
 *    but the app won't boot.
 *  - To change the partition scheme you MUST re-flash via USB.
 *
 * API server
 * ----------
 *  Cloud (default — no change needed): https://api.thingslinker.com
 *  Local dev: uncomment the line below and set your server's LAN IP:
 *    #define TL_API_SERVER "http://192.168.1.10:8000"
 *  This must appear BEFORE #include <ThingsLinker.h>.
 *  Alternatively, edit TL_API_SERVER in src/TL_Config.h once for all sketches.
 */

// ── Local dev only: uncomment and set your server's LAN IP ───────────────────
// #define TL_API_SERVER "http://192.168.1.10:8000"

#include <ThingsLinker.h>
#include <Adafruit_NeoPixel.h>

// ── Device credentials ────────────────────────────────────────────────────────
#define AUTH_TOKEN   "YOUR_DEVICE_AUTH_TOKEN"    // ~64 hex characters
#define BLUEPRINT_ID "YOUR_BLUEPRINT_ID"         // starts with "BLUE"

// ── Firmware version ──────────────────────────────────────────────────────────
static const char* FIRMWARE_VERSION = "4.0";

// ── NeoPixel config ───────────────────────────────────────────────────────────
#define RGB_PIN        4    // NeoPixel data pin
#define RGB_LED_COUNT  8    // Number of LEDs in your strip
#define MAX_BRIGHTNESS 30   // 0–100 %; raise only with external 5V power

Adafruit_NeoPixel strip(RGB_LED_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);

// ── OTA re-check interval ─────────────────────────────────────────────────────
static const unsigned long OTA_INTERVAL_MS = 60000UL;

// ── ThingsLinker instance ─────────────────────────────────────────────────────
ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

// ── RGB state ─────────────────────────────────────────────────────────────────
static uint8_t   _r = 255, _g = 255, _b = 255;
static bool      _on = false;
static uint16_t  _count = RGB_LED_COUNT;
static char      _pattern[32] = "Solid";

// ── Pattern timing ────────────────────────────────────────────────────────────
static bool          _blinkPhase   = true;
static unsigned long _lastBlink    = 0;
static int           _fadeBright   = 0;
static int           _fadeDir      = 1;
static unsigned long _lastFadeTick = 0;

static unsigned long _lastOtaCheck = 0;

// ─────────────────────────────────────────────────────────────────────────────
// applyStrip — push current state to the NeoPixel hardware
// ─────────────────────────────────────────────────────────────────────────────
void applyStrip(uint8_t r, uint8_t g, uint8_t b) {
  strip.clear();
  if (_on) {
    float  cap   = (_count < (uint16_t)RGB_LED_COUNT) ? _count : RGB_LED_COUNT;
    float  scale = (MAX_BRIGHTNESS / 100.0f);
    uint8_t sr = (uint8_t)(r * scale);
    uint8_t sg = (uint8_t)(g * scale);
    uint8_t sb = (uint8_t)(b * scale);
    strip.fill(strip.Color(sr, sg, sb), 0, (uint16_t)cap);
  }
  strip.show();
}

// ─────────────────────────────────────────────────────────────────────────────
// applyPattern — non-blocking effect engine, called every loop()
// ─────────────────────────────────────────────────────────────────────────────
void applyPattern() {
  if (!_on) { applyStrip(0, 0, 0); return; }

  unsigned long now = millis();

  // ── Blink: toggle on/off every 500 ms ─────────────────────────────────────
  if (strcmp(_pattern, "Blink") == 0) {
    if (now - _lastBlink >= 500UL) { _lastBlink = now; _blinkPhase = !_blinkPhase; }
    applyStrip(_blinkPhase ? _r : 0,
               _blinkPhase ? _g : 0,
               _blinkPhase ? _b : 0);
    return;
  }

  // ── Fade: smooth breathing — 256 steps × 8 ms ─────────────────────────────
  if (strcmp(_pattern, "Fade") == 0) {
    if (now - _lastFadeTick >= 8UL) {
      _lastFadeTick = now;
      _fadeBright  += _fadeDir;
      if (_fadeBright >= 255) { _fadeBright = 255; _fadeDir = -1; }
      if (_fadeBright <= 0)   { _fadeBright = 0;   _fadeDir =  1; }
    }
    float s = _fadeBright / 255.0f;
    applyStrip((uint8_t)(_r * s), (uint8_t)(_g * s), (uint8_t)(_b * s));
    return;
  }

  // ── Solid (default) ────────────────────────────────────────────────────────
  applyStrip(_r, _g, _b);
}

// ─────────────────────────────────────────────────────────────────────────────
// RGB widget callback — V0
// ─────────────────────────────────────────────────────────────────────────────
void onRGBWidget(uint8_t r, uint8_t g, uint8_t b,
                 bool on, uint16_t count, const char* pattern)
{
  _r = r; _g = g; _b = b; _on = on; _count = count;
  if (pattern && pattern[0] != '\0') {
    strncpy(_pattern, pattern, sizeof(_pattern) - 1);
    _pattern[sizeof(_pattern) - 1] = '\0';
  } else {
    strcpy(_pattern, "Solid");
  }
  _blinkPhase = true;
  _lastBlink  = millis();
  _fadeBright = 0;
  _fadeDir    = 1;
  Serial.printf("[RGB] R:%d G:%d B:%d | %s | %s | count:%d\n",
                r, g, b, on ? "ON" : "OFF", _pattern, count);
}

// ─────────────────────────────────────────────────────────────────────────────
// Switch widget callback — V1
// ─────────────────────────────────────────────────────────────────────────────
void onSwitchToggle(bool state) {
  _on = state;
  Serial.printf("[Switch V1] Strip %s\n", state ? "ON" : "OFF");
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  // ── NeoPixel init ─────────────────────────────────────────────────────────
  strip.begin();
  strip.clear();
  strip.show();

  Serial.println();
  Serial.println("========================================");
  Serial.printf( "  ThingsLinker OTA Firmware  v%s\n", FIRMWARE_VERSION);
  Serial.println("  NeoPixel RGB — OTA confirmed!");
  Serial.println("========================================");
  Serial.println("  V0 → RGB widget (color + pattern)");
  Serial.println("  V1 → Switch widget (on / off)");
  Serial.printf( "  Strip: %d LEDs on GPIO %d\n", RGB_LED_COUNT, RGB_PIN);
  Serial.println("========================================\n");

  // ── Connect to WiFi + MQTT ─────────────────────────────────────────────────
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();

  // ── Register callbacks AFTER begin() (same pattern as Full Dashboard) ──────
  iot.onRGB("V6",    onRGBWidget);
  iot.onSwitch("V8", onSwitchToggle);

  // ── Startup confirmation: 3 white blinks ──────────────────────────────────
  for (int i = 0; i < 3; i++) {
    strip.fill(strip.Color(80, 80, 80));  strip.show(); delay(150);
    strip.clear();                         strip.show(); delay(150);
  }

  _lastOtaCheck = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  iot.run();

  applyPattern();

  // ── Check for next OTA update every 60 s ──────────────────────────────────
  if (iot.wifiConnected() && (millis() - _lastOtaCheck >= OTA_INTERVAL_MS)) {
    _lastOtaCheck = millis();
    Serial.println("[OTA] Checking for firmware update...");
    OTAResult result = iot.checkOTA();
    if (result == OTA_NO_UPDATE) {
      Serial.printf("[OTA] Running v%s — up to date.\n", FIRMWARE_VERSION);
    }
  }

  delay(10);
}
