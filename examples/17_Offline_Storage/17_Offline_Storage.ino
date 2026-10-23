/**
 * ============================================================
 *   ThingsLinker — Offline Storage
 * ============================================================
 *
 *  Keeps readings while WiFi (or the server) is unavailable and
 *  sends them when the connection is back.
 *
 *    iot.offlineStorage(true);    // keep readings while offline
 *    iot.offlineStorage(false);   // default: they are dropped
 *
 *  With it on, nothing else changes in the sketch: publish as
 *  usual. While offline the values are stored; after the device
 *  reconnects they are sent oldest first, each with the time it
 *  was taken, so charts show them where they belong.
 *
 *  Try it: let the sketch run, switch your router off for a
 *  minute, then on again, and watch the Serial Monitor and the
 *  chart on the dashboard.
 *
 *  Good to know:
 *   - Readings are held in memory. They survive a connection
 *     loss of any length, but not a restart or a power cut.
 *   - 200 readings are kept by default. When the store is full,
 *     the oldest is dropped to make room for the newest. To keep
 *     more:  iot.offlineStorage(true, 500);
 *   - A reading uses about 100 bytes of RAM.
 *
 *  Widgets (add in the Organisation Portal):
 *    V1  Chart          Device→App  a value over time
 *    V2  Value Display  Device→App  readings waiting to be sent
 *
 *  Hardware: any ESP32 board.
 *
 *  CREDENTIALS: ThingsLinker org portal →
 *    Blueprints → [Blueprint] → Devices → [Device]
 * ============================================================
 */

#include <ThingsLinker.h>

// ── Credentials ──────────────────────────────────────────────
const char* AUTH_TOKEN   = "YOUR_AUTH_TOKEN";
const char* BLUEPRINT_ID = "YOUR_BLUEPRINT_ID";

// ── Offline storage: true = keep readings, false = drop them ──
const bool KEEP_READINGS_OFFLINE = true;

// ── Virtual pins ─────────────────────────────────────────────
#define PIN_VALUE    "V1"   // Chart
#define PIN_WAITING  "V2"   // Value Display

ThingsLinker iot(AUTH_TOKEN, BLUEPRINT_ID);

const unsigned long READ_INTERVAL = 5000;
unsigned long lastRead = 0;
float value = 25.0f;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println(  "║   ThingsLinker — Offline Storage         ║");
  Serial.println(  "╚══════════════════════════════════════════╝\n");

  // One line switches the feature on or off. It can be called before or
  // after begin(), and changed at any time.
  iot.offlineStorage(KEEP_READINGS_OFFLINE);

  // No MQTT login is needed for the ThingsLinker broker. Optional: if your own
  // broker asks for one, use iot.begin("MQTT_USERNAME", "MQTT_PASSWORD");
  iot.begin();
}

void loop() {
  iot.run();

  if (millis() - lastRead >= READ_INTERVAL) {
    lastRead = millis();

    // Replace with a real sensor reading
    value += random(-10, 11) / 10.0f;

    // Publish exactly as usual. Online: sent now. Offline: stored, and sent
    // after the connection returns.
    iot.chart(PIN_VALUE, value);

    Serial.printf("[Reading] %.1f   %s   waiting to be sent: %u\n",
                  value,
                  iot.mqttConnected() ? "online" : "OFFLINE",
                  (unsigned)iot.offlineCount());

    // Only worth showing while connected; offline it would just fill the store
    if (iot.mqttConnected()) iot.display(PIN_WAITING, (int)iot.offlineCount());
  }

  delay(10);
}
