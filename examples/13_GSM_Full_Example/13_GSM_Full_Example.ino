/**
 * @file 13_GSM_Full_Example.ino
 * @brief ThingsLinker GSM — full widget demonstration
 *
 * Demonstrates all major ThingsLinkerGSM features:
 *   Publish  : gauge, chart, display, label, button, led, map
 *   Subscribe: switch, slider, rgb, joystick
 *   Storage  : persist a counter across reboots
 *   OTA      : check for firmware updates
 *   Status   : signal quality, MQTT state, IP address
 *
 * ── Hardware ──────────────────────────────────────────────────────────────────
 *   Board   : ESP32 (WROOM-32 / DevKit)
 *   Modem   : Simcom A7672 / SIM7672 / SIM7600-family
 *   Sensors : DHT22 on GPIO5 (temperature + humidity)
 *             LED on GPIO2 (built-in)
 *             Relay on GPIO26
 *
 * ── Wiring ────────────────────────────────────────────────────────────────────
 *   ESP32 GPIO16 ── A7672 TX      ESP32 GPIO17 ── A7672 RX
 *   ESP32 GPIO4  ── A7672 PWRKEY  External 4V / 500 mA ── A7672 VCC
 */

// ─────────────────────────────────────────────────────────────────────────────
// Credentials — replace with your actual values from the Organisation Portal
// ─────────────────────────────────────────────────────────────────────────────
#define AUTH_TOKEN    "your_device_auth_token"
#define BLUEPRINT_KEY "your_blueprint_key"
#define CLIENT_KEY    "your_client_key"
#define SECRET_KEY    "your_secret_key"

#define APN           "airtelgprs.com"   // your SIM card APN
#define APN_USER      ""
#define APN_PASS      ""

// ─────────────────────────────────────────────────────────────────────────────
// Pin definitions
// ─────────────────────────────────────────────────────────────────────────────
#define MODEM_RX_PIN   16
#define MODEM_TX_PIN   17

// PWRKEY — set to TL_NO_PWRKEY if your board powers the modem automatically
#define MODEM_PWR_PIN   4   // or: TL_NO_PWRKEY

#define LED_PIN         2    // built-in LED
#define RELAY_PIN      26    // relay / output
#define PWM_PIN        25    // PWM output for slider dimming

// ─────────────────────────────────────────────────────────────────────────────
// Virtual pins (dashboard widget assignments)
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_TEMP        "V1"    // Gauge  — temperature
#define PIN_HUMIDITY    "V2"    // Gauge  — humidity
#define PIN_CHART_TEMP  "V3"    // Chart  — temperature history
#define PIN_UPTIME      "V4"    // Display — uptime in minutes
#define PIN_SIGNAL      "V5"    // Display — GSM signal quality
#define PIN_IP_LABEL    "V6"    // Label  — IP address (use Label widget)
#define PIN_SWITCH      "V7"    // Switch — relay control
#define PIN_DIMMER      "V8"    // Slider — LED brightness (0–100)
#define PIN_RGB         "V9"    // RGB    — RGB LED strip
#define PIN_JOYSTICK    "V10"   // Joystick

#include <ThingsLinkerGSM.h>

ThingsLinkerGSM iot(Serial2, AUTH_TOKEN, BLUEPRINT_KEY);

// PWM channel for LED dimming (ESP32 LEDC)
#define PWM_CHANNEL   0
#define PWM_FREQUENCY 5000
#define PWM_RESOLUTION  8   // 8-bit: 0–255

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks — called when the app sends data to the device
// ─────────────────────────────────────────────────────────────────────────────

void onSwitchChange(bool on) {
  Serial.println("[Switch] Relay → " + String(on ? "ON" : "OFF"));
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

void onDimmerChange(float percent) {
  // Slider range is 0–100 from the app
  int duty = (int)(percent * 2.55f);   // scale to 0–255
  duty = constrain(duty, 0, 255);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PWM_PIN, duty);       // ESP32 core 3.x addresses PWM by pin
#else
  ledcWrite(PWM_CHANNEL, duty);   // ESP32 core 2.x addresses PWM by channel
#endif
  Serial.printf("[Slider] Dimmer: %.0f%%  duty: %d\n", percent, duty);
}

void onRGBChange(uint8_t r, uint8_t g, uint8_t b, bool on,
                 uint16_t count, const char* pattern) {
  Serial.printf("[RGB] on=%d  r=%d g=%d b=%d  count=%d  pattern=%s\n",
                on, r, g, b, count, pattern);
  // Drive your WS2812 / NeoPixel strip here using FastLED or Adafruit NeoPixel
  // Example (FastLED):
  //   for (int i = 0; i < count && i < NUM_LEDS; i++) {
  //     leds[i] = CRGB(r, g, b);
  //   }
  //   FastLED.show();
}

void onJoystickMove(float x, float y) {
  // x and y are in the range −1.0 … +1.0
  // Map to motor speeds, servo angles, etc.
  Serial.printf("[Joystick] x=%.2f  y=%.2f\n", x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial2.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // Configure output pins
  pinMode(LED_PIN,   OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PWM_PIN, PWM_FREQUENCY, PWM_RESOLUTION);       // ESP32 core 3.x
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);    // ESP32 core 2.x
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
#endif

  // Register all callbacks before begin()
  iot.onSwitch  (PIN_SWITCH,   onSwitchChange);
  iot.onSlider  (PIN_DIMMER,   onDimmerChange);
  iot.onRGB     (PIN_RGB,      onRGBChange);
  iot.onJoystick(PIN_JOYSTICK, onJoystickMove);

  // Connect to ThingsLinker
  iot.begin(CLIENT_KEY, SECRET_KEY, APN, MODEM_PWR_PIN, APN_USER, APN_PASS);

  if (iot.mqttConnected()) {
    Serial.println(">>> ThingsLinker GSM ready!");
    Serial.println("    IP     : " + iot.getIP());
    Serial.println("    Signal : " + String(iot.signalQuality()) + "/31");
    Serial.println("    Chip   : " + iot.getChipID());
  }

  // Read boot counter from NVS
  int boots = iot.getInt("boot_count", 0) + 1;
  iot.saveInt("boot_count", boots);
  Serial.println("Boot count: " + String(boots));
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  iot.run();

  static unsigned long lastPublish = 0;
  static unsigned long lastOTA     = 0;

  unsigned long now = millis();

  // Publish telemetry every 10 seconds
  if (now - lastPublish >= 10000UL) {
    lastPublish = now;

    // ── Sensor readings (replace with real sensor code) ──────────────────
    float temperature = 22.0f + random(-20, 80) / 10.0f;   // 20–30 °C
    float humidity    = 55.0f + random(-100, 100) / 10.0f; // 45–65 %

    // ── Publish to dashboard ─────────────────────────────────────────────
    iot.gauge(PIN_TEMP,       temperature);
    iot.gauge(PIN_HUMIDITY,   humidity);
    iot.chart(PIN_CHART_TEMP, temperature);
    iot.display(PIN_UPTIME,   (float)(now / 60000UL));   // minutes since boot
    iot.display(PIN_SIGNAL,   (float)iot.signalQuality());
    iot.label(PIN_IP_LABEL,   iot.getIP());              // Label accepts text

    Serial.printf("[Publish] temp=%.1f°C  hum=%.1f%%  uptime=%lum  signal=%d/31\n",
                  temperature, humidity, now / 60000UL, iot.signalQuality());
  }

  // Check for OTA firmware updates every 5 minutes
  if (now - lastOTA >= 300000UL) {
    lastOTA = now;
    Serial.println("[OTA] Checking for firmware update...");
    OTAResult r = iot.checkOTA();
    if (r == OTA_NO_UPDATE) Serial.println("[OTA] Firmware is up to date");
    if (r == OTA_FAILED)    Serial.println("[OTA] Update failed — will retry");
    if (r == OTA_ERROR)     Serial.println("[OTA] Could not reach server");
    // OTA_SUCCESS never reaches here — device restarts automatically
  }
}
