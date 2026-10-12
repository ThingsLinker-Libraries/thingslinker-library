/**
 * @file 18_GSM_Offline_Storage.ino
 * @brief ThingsLinker GSM — keep readings while there is no mobile signal
 *
 * Mobile coverage comes and goes. With offline storage on, readings taken
 * while there is no data connection are kept and sent when it is back:
 *
 *   iot.offlineStorage(true);    // keep readings while offline
 *   iot.offlineStorage(false);   // default: they are dropped
 *
 * Nothing else changes in the sketch: publish as usual. After the device
 * reconnects, the stored readings are sent oldest first, each with the time
 * it was taken, so charts show them where they belong.
 *
 * Good to know:
 *   - Readings are held in memory. They survive a loss of signal of any
 *     length, but not a restart or a power cut.
 *   - 200 readings are kept by default; when the store is full the oldest is
 *     dropped. To keep more:  iot.offlineStorage(true, 500);
 *   - Each stored reading costs mobile data when it is finally sent.
 *
 * ── Widgets (add in the Organisation Portal) ─────────────────────────────────
 *   V1  Chart          Device→App  a value over time
 *   V2  Value Display  Device→App  readings waiting to be sent
 *
 * ── Wiring ────────────────────────────────────────────────────────────────────
 *   ESP32 GPIO16 (RX2) ────── A7672 TX
 *   ESP32 GPIO17 (TX2) ────── A7672 RX
 *   ESP32 GPIO4        ────── A7672 PWRKEY
 *   ESP32 GND          ────── A7672 GND
 *   (see 12_GSM_Connection for power notes)
 */

#define AUTH_TOKEN    "your_device_auth_token"
#define BLUEPRINT_KEY "your_blueprint_key"       // starts with "BLUE"
#define CLIENT_KEY    "your_client_key"
#define SECRET_KEY    "your_secret_key"

#define APN           "airtelgprs.com"
#define APN_USER      ""
#define APN_PASS      ""

#define MODEM_RX_PIN   16   // ESP32 GPIO16 ← A7672 TX
#define MODEM_TX_PIN   17   // ESP32 GPIO17 → A7672 RX
#define MODEM_PWR_PIN   4   // GPIO 4 → A7672 PWRKEY (or TL_NO_PWRKEY)

// Offline storage: true = keep readings, false = drop them
#define KEEP_READINGS_OFFLINE true

#include <ThingsLinkerGSM.h>

ThingsLinkerGSM iot(Serial2, AUTH_TOKEN, BLUEPRINT_KEY);

#define PIN_VALUE    "V1"   // Chart
#define PIN_WAITING  "V2"   // Value Display

const unsigned long READ_INTERVAL = 15000;
unsigned long lastRead = 0;
float value = 25.0f;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial2.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // One line switches the feature on or off. It can be called before or
  // after begin(), and changed at any time.
  iot.offlineStorage(KEEP_READINGS_OFFLINE);

  iot.begin(CLIENT_KEY, SECRET_KEY, APN, MODEM_PWR_PIN, APN_USER, APN_PASS);

  if (!iot.mqttConnected()) {
    Serial.println("\n>>> Not connected yet — readings will be stored and sent later <<<");
  }
}

void loop() {
  iot.run();   // MQTT, auto-reconnect, and sending stored readings

  if (millis() - lastRead >= READ_INTERVAL) {
    lastRead = millis();

    // Replace with a real sensor reading
    value += random(-10, 11) / 10.0f;

    // Publish exactly as usual. Online: sent now. Offline: stored, and sent
    // after the connection returns.
    iot.chart(PIN_VALUE, value);

    Serial.printf("[Reading] %.1f   %s   waiting to be sent: %u   signal: %d/31\n",
                  value,
                  iot.mqttConnected() ? "online" : "OFFLINE",
                  (unsigned)iot.offlineCount(),
                  iot.signalQuality());

    if (iot.mqttConnected()) iot.display(PIN_WAITING, (int)iot.offlineCount());
  }
}
