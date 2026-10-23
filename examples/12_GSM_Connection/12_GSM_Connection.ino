/**
 * @file 12_GSM_Connection.ino
 * @brief ThingsLinker GSM — basic connection test with Simcom A7672 / SIM7672
 *
 * This sketch connects an ESP32 to ThingsLinker via a Simcom A7672 / SIM7672
 * 4G LTE modem using the ThingsLinkerGSM class.  It publishes a fake
 * temperature reading every 5 seconds and listens for a Switch widget.
 *
 * ── Hardware ──────────────────────────────────────────────────────────────────
 *   Board : ESP32 (any variant — WROOM-32, S3, C3, etc.)
 *   Modem : Simcom A7672SA / A7672E / SIM7672G (or any SIM7600-family module)
 *
 * ── Wiring ────────────────────────────────────────────────────────────────────
 *   ESP32 GPIO16 (RX2) ────── A7672 TX
 *   ESP32 GPIO17 (TX2) ────── A7672 RX
 *   ESP32 GPIO4        ────── A7672 PWRKEY
 *   ESP32 3V3          ────── A7672 VCC  (use external supply if modem needs >500 mA)
 *   ESP32 GND          ────── A7672 GND
 *
 *   Note: some A7672 breakout boards accept 5 V on VCC and regulate internally.
 *
 * ── Required libraries (install via Arduino Library Manager) ─────────────────
 *   ArduinoJson   — search "ArduinoJson"   by Benoit Blanchon
 *   PubSubClient  — search "PubSubClient"  by Nick O'Leary  (WiFi variant only)
 *
 *   ThingsLinkerGSM uses native A76XX AT+CMQTT* commands — TinyGSM NOT required.
 *
 * ── Getting your credentials ─────────────────────────────────────────────────
 *   Organisation Portal → Blueprints → [Blueprint] → Devices → [Device]
 *   Copy: Auth Token, Blueprint Key
 */

// ─────────────────────────────────────────────────────────────────────────────
// 1. Credentials — replace with your actual values
// ─────────────────────────────────────────────────────────────────────────────
#define AUTH_TOKEN    "your_device_auth_token"
#define BLUEPRINT_KEY "your_blueprint_key"       // starts with "BLUE"

// ─────────────────────────────────────────────────────────────────────────────
// 2. APN — set to your SIM card's APN
//    Common APNs:
//      Airtel India : "airtelgprs.com"
//      Jio India    : "jionet"
//      Vodafone IN  : "www"
//      AT&T US      : "phone"
//      T-Mobile US  : "fast.t-mobile.com"
// ─────────────────────────────────────────────────────────────────────────────
#define APN           "airtelgprs.com"
#define APN_USER      ""   // leave empty for most carriers
#define APN_PASS      ""   // leave empty for most carriers

// ─────────────────────────────────────────────────────────────────────────────
// 3. Hardware pins
// ─────────────────────────────────────────────────────────────────────────────
#define MODEM_RX_PIN   16   // ESP32 GPIO16 ← A7672 TX
#define MODEM_TX_PIN   17   // ESP32 GPIO17 → A7672 RX

// PWRKEY — choose ONE of the two options below:
//
//   Option A: PWRKEY connected to an ESP32 GPIO (library pulses it on startup)
#define MODEM_PWR_PIN   4   // GPIO 4 → A7672 PWRKEY
//
//   Option B: PWRKEY not connected (modem auto-powers when VCC is applied,
//             or PWRKEY is tied directly to VCC on your breakout board)
// #define MODEM_PWR_PIN TL_NO_PWRKEY

// ─────────────────────────────────────────────────────────────────────────────
// Library include
// ─────────────────────────────────────────────────────────────────────────────
#include <ThingsLinkerGSM.h>

// Create the ThingsLinkerGSM instance — pass Serial2 (UART2 on ESP32)
ThingsLinkerGSM iot(Serial2, AUTH_TOKEN, BLUEPRINT_KEY);

// ─────────────────────────────────────────────────────────────────────────────
// Pin assignments (virtual pins on the ThingsLinker dashboard)
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_TEMP    "V1"   // Gauge or Chart widget — temperature
#define PIN_SWITCH  "V2"   // Switch widget — relay / LED control

// Example output pin controlled by the Switch widget
#define LED_PIN 2   // built-in LED on most ESP32 boards

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  // Initialise Serial2 for the modem BEFORE calling iot.begin()
  Serial2.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  pinMode(LED_PIN, OUTPUT);

  // Register widget callbacks BEFORE begin() — they are stored and
  // automatically re-subscribed after every MQTT reconnect.
  iot.onSwitch(PIN_SWITCH, [](bool on) {
    Serial.println("[App] Switch → " + String(on ? "ON" : "OFF"));
    digitalWrite(LED_PIN, on ? HIGH : LOW);
  });

  // iot.debug(false);  // uncomment to suppress Serial debug output

  // Begin: power modem, register network, connect MQTT
  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, put the username and password first:
  //   iot.begin("MQTT_USERNAME", "MQTT_PASSWORD", APN, MODEM_PWR_PIN, APN_USER, APN_PASS);
  iot.begin(APN, MODEM_PWR_PIN, APN_USER, APN_PASS);

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

  // Publish a simulated temperature reading every 5 seconds
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish >= 5000UL) {
    lastPublish = millis();

    // Replace with a real sensor reading (e.g. DHT22, DS18B20, etc.)
    float temperature = 20.0f + random(0, 150) / 10.0f;   // 20.0 – 34.9 °C

    iot.gauge(PIN_TEMP, temperature);
    Serial.printf("[Publish] Temperature: %.1f °C  |  MQTT: %s  |  Signal: %d/31\n",
                  temperature,
                  iot.mqttConnected() ? "OK" : "RECONNECTING",
                  iot.signalQuality());
  }
}
