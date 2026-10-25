/**
 * @file TL_MQTT.cpp
 * @brief MQTT communication over TLS — publish sensor data, subscribe to widget controls
 */

#include "TL_MQTT.h"
#include "TL_Echo.h"
#include "TL_Config.h"
#include "TL_Codec.h"
#include "TL_Offline.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#ifdef ESP32
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecureBearSSL.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// TLS client + MQTT client
// ─────────────────────────────────────────────────────────────────────────────
// setInsecure() skips certificate verification, which is acceptable for ESP32
// IoT devices connecting to a known, fixed broker URL. For stricter security,
// replace with wifiClient.setCACert(your_ca_pem).
#ifdef ESP32
  static WiFiClientSecure _wifiClient;
#elif defined(ESP8266)
  static BearSSL::WiFiClientSecure _wifiClient;
#endif

static PubSubClient _mqtt(_wifiClient);

// ─────────────────────────────────────────────────────────────────────────────
// Device identity (set once in connectMQTT, reused by subscribe/publish)
// ─────────────────────────────────────────────────────────────────────────────
static String _authToken   = "";
static String _blueprintId = "";

// ─────────────────────────────────────────────────────────────────────────────
// Callback tables
// ─────────────────────────────────────────────────────────────────────────────
struct FloatCallback {
  char pin[8];           // "V0" … "V124"
  char widgetType[20];   // "Button", "Slider", …
  void (*fn)(float);
};

struct BoolCallback {
  char pin[8];
  char widgetType[20];
  void (*fn)(bool);
};

// Text widgets (Terminal): receives the message as a string
struct TextCallback {
  char pin[8];
  char widgetType[20];
  void (*fn)(const char*);
};

// RGB widget: receives r, g, b channels + on/off + LED count + pattern name
struct RGBCallback {
  char pin[8];
  void (*fn)(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern);
};

// Joystick widget: receives x, y axis values
struct JoystickCallback {
  char pin[8];
  void (*fn)(float x, float y);
};

static FloatCallback    _floatCbs[MAX_SUBSCRIPTIONS];
static int              _floatCbCount    = 0;

static BoolCallback     _boolCbs[MAX_SUBSCRIPTIONS];
static int              _boolCbCount     = 0;

static TextCallback     _textCbs[MAX_SUBSCRIPTIONS];
static int              _textCbCount     = 0;

static RGBCallback      _rgbCbs[MAX_SUBSCRIPTIONS];
static int              _rgbCbCount      = 0;

static JoystickCallback _joystickCbs[MAX_SUBSCRIPTIONS];
static int              _joystickCbCount = 0;

static void (*_commandCb)(const char* cmd) = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Return the best available Unix timestamp.
 * Uses NTP-synced wall-clock time when available; falls back to millis()-based
 * seconds since boot if NTP has not yet synced. The fallback value is never
 * zero (millis() runs from reset), which keeps the backend's monotonicity
 * check happy on very early payloads.
 */
static unsigned long getTimestamp() {
  time_t now = time(nullptr);
  return (now > (time_t)NTP_VALID_EPOCH) ? (unsigned long)now : (millis() / 1000UL);
}

/**
 * Build the standard ThingsLinker MQTT topic.
 * Format: device/{WidgetType}/{BlueprintId}/{AuthToken}/{VirtualPin}/
 */
static String buildTopic(const char* widgetType, const char* pin) {
  return String("device/") + widgetType + "/" + _blueprintId + "/" + _authToken + "/" + pin + "/";
}

static String statusTopic()  { return "device/status/"  + _blueprintId + "/" + _authToken + "/"; }
static String commandTopic() { return "device/command/" + _blueprintId + "/" + _authToken + "/"; }

/** True once NTP has set the clock, i.e. getTimestamp() is real time. */
static bool clockIsSet() {
  return time(nullptr) > (time_t)NTP_VALID_EPOCH;
}

static unsigned long _connectedAt   = 0;   // millis() of the last successful connect
static unsigned long _statusSentAt  = 0;   // millis() ONLINE was last published
static unsigned long _lastStoredSend = 0;  // millis() a stored reading was last sent

/**
 * Send one reading now, or keep it for later when offline storage is on.
 *
 * While stored readings are still waiting, a new one joins the end of the
 * queue instead of overtaking them, so everything arrives in the order it
 * was taken.
 *
 * @return true when the reading was sent or stored
 */
static bool sendOrStore(const char* widgetType, const char* pin, const char* payload) {
  bool connected = _mqtt.connected();

  if (connected && tlOfflineCount() == 0) {
    String topic = buildTopic(widgetType, pin);
    if (_mqtt.publish(topic.c_str(), payload, false)) {
      tlEchoNote(topic.c_str(), payload);
      return true;
    }
  }

  if (tlOfflineEnabled()) {
    tlOfflineStore(widgetType, pin, payload, clockIsSet());
    TL_LOG("[Offline] Stored " + String(pin) + " (" + String(tlOfflineCount()) + " waiting)");
    return true;
  }

  if (connected) TL_LOG("[MQTT] ✗ Publish failed (" + String(pin) + ")");
  return false;
}

/** Send stored readings, oldest first, a little at a time. Called from loopMQTT(). */
static void sendStoredReadings() {
  if (tlOfflineCount() == 0 || !_mqtt.connected()) return;

  unsigned long now = millis();
  if (now - _lastStoredSend < TL_OFFLINE_SEND_INTERVAL_MS) return;

  // Readings taken before the clock was set need it to get their real time.
  // Give NTP a moment after reconnecting; send anyway if it never arrives.
  if (tlOfflineFrontNeedsClock() && !clockIsSet() &&
      now - _connectedAt < TL_OFFLINE_CLOCK_WAIT_MS) return;

  char widgetType[20];
  char pin[8];
  char payload[TL_PAYLOAD_BUFFER_SIZE];
  if (!tlOfflinePeek(widgetType, sizeof(widgetType), pin, sizeof(pin), payload, sizeof(payload),
                     clockIsSet() ? (unsigned long)time(nullptr) : 0UL)) {
    tlOfflinePop();   // unreadable record: drop it so the queue cannot stall
    return;
  }

  _lastStoredSend = now;
  String topic = buildTopic(widgetType, pin);
  if (!_mqtt.publish(topic.c_str(), payload, false)) return;   // try again next time
  tlEchoNote(topic.c_str(), payload);

  tlOfflinePop();
  if (tlOfflineCount() == 0) TL_LOG("[Offline] ✓ All stored readings sent");
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT message callback
// ─────────────────────────────────────────────────────────────────────────────
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // Convert to null-terminated string
  char buf[length + 1];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  // Our own status topic. An OFFLINE there while we are connected is out of
  // date: the last will of an earlier connection of this device, delivered
  // late. Say ONLINE again so dashboards show the device as it is.
  String ownStatus = statusTopic();
  if (ownStatus == topic) {
    if (strcmp(buf, "OFFLINE") == 0 && _mqtt.publish(ownStatus.c_str(), "ONLINE", true)) {
      _statusSentAt = millis();
      TL_LOG("[MQTT] ✓ Status → ONLINE (replaced an old OFFLINE)");
    }
    return;
  }

  // A command from the app or portal, e.g. {"cmd":"reset_wifi","t":...}
  if (commandTopic() == topic) {
    if (length == 0 || !_commandCb) return;   // empty: our own clearing of the topic
    JsonDocument cmdDoc;
    if (deserializeJson(cmdDoc, buf) != DeserializationError::Ok) return;
    const char* cmd = cmdDoc["cmd"] | "";
    if (cmd[0] == '\0') return;
    if (clockIsSet() && !cmdDoc["t"].isNull()) {
      long age = (long)time(nullptr) - cmdDoc["t"].as<long>();
      if (age > (long)TL_COMMAND_MAX_AGE_S || age < -(long)TL_COMMAND_MAX_AGE_S) {
        TL_LOG("[MQTT] Ignored an out-of-date command: " + String(cmd));
        return;
      }
    }
    TL_LOG("[MQTT] Command received: " + String(cmd));
    _commandCb(cmd);
    return;
  }

  TL_LOG("[MQTT] Received on: " + String(topic));
  TL_LOG("[MQTT] Payload: " + String(buf));

  // The device's own message coming back is not a command from the app
  if (tlEchoIsOwn(topic, buf)) return;

  // Parse JSON (ArduinoJson 7 sizes the document automatically)
  JsonDocument doc;
  if (deserializeJson(doc, buf) != DeserializationError::Ok) {
    TL_LOG("[MQTT] JSON parse error — ignored");
    return;
  }
  if (doc["v"].isNull()) {
    TL_LOG("[MQTT] No 'v' field — ignored");
    return;
  }

  // "v" may be a number, a boolean or a string depending on the widget data type
  float value = tlNumberOf(doc);

  // Extract virtual pin from topic segment [4]
  // Topic: device/{WidgetType}/{BlueprintId}/{AuthToken}/{VirtualPin}/
  //         [0]    [1]           [2]           [3]         [4]
  String t = String(topic);
  int slashes[5];
  int found = 0;
  for (int i = 0; i < (int)t.length() && found < 5; i++) {
    if (t[i] == '/') slashes[found++] = i;
  }
  if (found < 5) return;

  String pin = t.substring(slashes[3] + 1, slashes[4]);
  TL_LOG("[MQTT] Pin=" + pin + "  value=" + String(value));

  // RGB widget: payload contains separate r, g, b fields (and optional count)
  if (!doc["r"].isNull() && !doc["g"].isNull() && !doc["b"].isNull()) {
    for (int i = 0; i < _rgbCbCount; i++) {
      if (pin == _rgbCbs[i].pin && _rgbCbs[i].fn) {
        uint8_t     r       = doc["r"].as<uint8_t>();
        uint8_t     g       = doc["g"].as<uint8_t>();
        uint8_t     b       = doc["b"].as<uint8_t>();
        uint16_t    count   = !doc["count"].isNull()   ? doc["count"].as<uint16_t>()      : 1;
        const char* pattern = !doc["pattern"].isNull() ? doc["pattern"].as<const char*>() : "";
        _rgbCbs[i].fn(r, g, b, value > 0.0f, count, pattern);
        return;
      }
    }
  }

  // Joystick widget: payload contains separate x, y fields
  if (!doc["x"].isNull() && !doc["y"].isNull()) {
    for (int i = 0; i < _joystickCbCount; i++) {
      if (pin == _joystickCbs[i].pin && _joystickCbs[i].fn) {
        float x = doc["x"].as<float>();
        float y = doc["y"].as<float>();
        _joystickCbs[i].fn(x, y);
        return;
      }
    }
  }

  // Text callbacks (Terminal)
  for (int i = 0; i < _textCbCount; i++) {
    if (pin == _textCbs[i].pin && _textCbs[i].fn) {
      char scratch[32];
      _textCbs[i].fn(tlTextOf(doc, scratch, sizeof(scratch)));
      return;
    }
  }

  // Bool callbacks (Button, Switch)
  for (int i = 0; i < _boolCbCount; i++) {
    if (pin == _boolCbs[i].pin && _boolCbs[i].fn) {
      _boolCbs[i].fn(tlBoolOf(doc));
      return;
    }
  }

  // Float callbacks (Slider, Timer, Value Display, etc.)
  for (int i = 0; i < _floatCbCount; i++) {
    if (pin == _floatCbs[i].pin && _floatCbs[i].fn) {
      _floatCbs[i].fn(value);
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool connectMQTT(const char* authTokenParam, const char* blueprintIdParam,
                 const char* mqttUsername,       const char* mqttPassword) {
  _authToken   = String(authTokenParam   ? authTokenParam   : "");
  _blueprintId = String(blueprintIdParam ? blueprintIdParam : "");

  TL_LOG("[MQTT] Connecting to " MQTT_SERVER ":" + String(MQTT_PORT));

  _wifiClient.setInsecure();
  _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  _mqtt.setCallback(onMqttMessage);
  _mqtt.setKeepAlive(MQTT_KEEPALIVE);
  _mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);

  String clientId    = "ThingsLinker_" + getChipID();
  String ownStatus   = statusTopic();

  TL_LOG("[MQTT] Client ID: " + clientId);
  TL_LOG("[MQTT] Status topic: " + ownStatus);

  bool ok;
  bool hasAuth = (mqttUsername && mqttUsername[0] != '\0') &&
                 (mqttPassword && mqttPassword[0] != '\0');

  if (hasAuth) {
    TL_LOG("[MQTT] Logging in with the MQTT username...");
    ok = _mqtt.connect(
      clientId.c_str(),
      mqttUsername,
      mqttPassword,
      ownStatus.c_str(), 0, true, "OFFLINE"
    );
  } else {
    TL_LOG("[MQTT] Connecting anonymously...");
    ok = _mqtt.connect(
      clientId.c_str(),
      ownStatus.c_str(), 0, true, "OFFLINE"
    );
  }

  if (!ok) {
    int state = _mqtt.state();
    TL_LOG("[MQTT] ✗ Failed — state: " + String(state));
    switch (state) {
      case -4: TL_LOG("[MQTT]   Connection timeout"); break;
      case -3: TL_LOG("[MQTT]   Connection lost");   break;
      case -2: TL_LOG("[MQTT]   Connect failed");    break;
      case -1: TL_LOG("[MQTT]   Disconnected");  break;
      case  1: TL_LOG("[MQTT]   Bad protocol version"); break;
      case  2: TL_LOG("[MQTT]   Client ID rejected"); break;
      case  3: TL_LOG("[MQTT]   Server unavailable"); break;
      case  4: TL_LOG("[MQTT]   Bad username or password (the ThingsLinker broker needs none: call iot.begin())"); break;
      case  5: TL_LOG("[MQTT]   Not authorized (the ThingsLinker broker needs no login: call iot.begin())"); break;
    }
    return false;
  }

  TL_LOG("[MQTT] ✓ Connected!");
  _connectedAt = millis();
  if (tlOfflineCount() > 0) {
    TL_LOG("[Offline] Sending " + String(tlOfflineCount()) + " stored readings...");
  }

  // Publish ONLINE status immediately (retained so dashboard sees it instantly)
  _mqtt.publish(ownStatus.c_str(), "ONLINE", true);
  _statusSentAt = millis();
  // Follow our own status too, to answer an out-of-date OFFLINE (see onMqttMessage)
  _mqtt.subscribe(ownStatus.c_str(), 1);
  TL_LOG("[MQTT] ✓ Status → ONLINE");

  // Commands from the app and portal. Commands are sent once, never retained;
  // clearing the topic before subscribing means a command someone did retain
  // cannot reset the device again each time it connects.
  if (_commandCb) {
    String cmdTopic = commandTopic();
    _mqtt.publish(cmdTopic.c_str(), "", true);
    _mqtt.subscribe(cmdTopic.c_str(), 1);
    TL_LOG("[MQTT] ✓ Listening for commands");
  }

  // Re-subscribe all registered callbacks (required after every reconnect)
  for (int i = 0; i < _boolCbCount; i++) {
    String topic = buildTopic(_boolCbs[i].widgetType, _boolCbs[i].pin);
    _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG("[MQTT] ✓ Subscribed: " + topic);
  }
  for (int i = 0; i < _floatCbCount; i++) {
    String topic = buildTopic(_floatCbs[i].widgetType, _floatCbs[i].pin);
    _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG("[MQTT] ✓ Subscribed: " + topic);
  }
  for (int i = 0; i < _textCbCount; i++) {
    String topic = buildTopic(_textCbs[i].widgetType, _textCbs[i].pin);
    _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG("[MQTT] ✓ Subscribed: " + topic);
  }
  for (int i = 0; i < _rgbCbCount; i++) {
    String topic = buildTopic("RGB", _rgbCbs[i].pin);
    _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG("[MQTT] ✓ Subscribed: " + topic);
  }
  for (int i = 0; i < _joystickCbCount; i++) {
    String topic = buildTopic("Joystick", _joystickCbs[i].pin);
    _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG("[MQTT] ✓ Subscribed: " + topic);
  }

  return true;
}

void onMQTTCommand(void (*callback)(const char* cmd)) {
  _commandCb = callback;
}

void disconnectMQTT() {
  if (_mqtt.connected()) {
    // A clean disconnect does not send the last will, so say OFFLINE ourselves
    _mqtt.publish(statusTopic().c_str(), "OFFLINE", true);
    _mqtt.disconnect();
    TL_LOG("[MQTT] Disconnected");
  }
}

bool isMQTTConnected() {
  return _mqtt.connected();
}

void subscribeMQTT(const char* widgetType, const char* pin, void (*callback)(float)) {
  if (_floatCbCount >= MAX_SUBSCRIPTIONS) {
    TL_LOG("[MQTT] ✗ Subscription limit reached (" + String(MAX_SUBSCRIPTIONS) + ")");
    return;
  }

  FloatCallback& cb = _floatCbs[_floatCbCount++];
  strncpy(cb.pin,        pin,        sizeof(cb.pin)        - 1);
  strncpy(cb.widgetType, widgetType, sizeof(cb.widgetType) - 1);
  cb.pin[sizeof(cb.pin) - 1]               = '\0';
  cb.widgetType[sizeof(cb.widgetType) - 1] = '\0';
  cb.fn = callback;

  if (_mqtt.connected()) {
    String topic = buildTopic(widgetType, pin);
    bool ok = _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG(ok ? "[MQTT] ✓ Subscribed: " + topic : "[MQTT] ✗ Subscribe failed: " + topic);
  }
}

void subscribeMQTTButton(const char* widgetType, const char* pin, void (*callback)(bool)) {
  if (_boolCbCount >= MAX_SUBSCRIPTIONS) {
    TL_LOG("[MQTT] ✗ Subscription limit reached (" + String(MAX_SUBSCRIPTIONS) + ")");
    return;
  }

  BoolCallback& cb = _boolCbs[_boolCbCount++];
  strncpy(cb.pin,        pin,        sizeof(cb.pin)        - 1);
  strncpy(cb.widgetType, widgetType, sizeof(cb.widgetType) - 1);
  cb.pin[sizeof(cb.pin) - 1]               = '\0';
  cb.widgetType[sizeof(cb.widgetType) - 1] = '\0';
  cb.fn = callback;

  if (_mqtt.connected()) {
    String topic = buildTopic(widgetType, pin);
    bool ok = _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG(ok ? "[MQTT] ✓ Subscribed: " + topic : "[MQTT] ✗ Subscribe failed: " + topic);
  }
}

void subscribeMQTTText(const char* widgetType, const char* pin, void (*callback)(const char*)) {
  if (_textCbCount >= MAX_SUBSCRIPTIONS) {
    TL_LOG("[MQTT] ✗ Subscription limit reached (" + String(MAX_SUBSCRIPTIONS) + ")");
    return;
  }

  TextCallback& cb = _textCbs[_textCbCount++];
  strncpy(cb.pin,        pin,        sizeof(cb.pin)        - 1);
  strncpy(cb.widgetType, widgetType, sizeof(cb.widgetType) - 1);
  cb.pin[sizeof(cb.pin) - 1]               = '\0';
  cb.widgetType[sizeof(cb.widgetType) - 1] = '\0';
  cb.fn = callback;

  if (_mqtt.connected()) {
    String topic = buildTopic(widgetType, pin);
    bool ok = _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG(ok ? "[MQTT] ✓ Subscribed: " + topic : "[MQTT] ✗ Subscribe failed: " + topic);
  }
}

void subscribeRGBMQTT(const char* pin, void (*callback)(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern)) {
  if (_rgbCbCount >= MAX_SUBSCRIPTIONS) {
    TL_LOG("[MQTT] ✗ Subscription limit reached");
    return;
  }
  RGBCallback& cb = _rgbCbs[_rgbCbCount++];
  strncpy(cb.pin, pin, sizeof(cb.pin) - 1);
  cb.pin[sizeof(cb.pin) - 1] = '\0';
  cb.fn = callback;

  if (_mqtt.connected()) {
    String topic = buildTopic("RGB", pin);
    bool ok = _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG(ok ? "[MQTT] ✓ Subscribed: " + topic : "[MQTT] ✗ Subscribe failed: " + topic);
  }
}

void subscribeJoystickMQTT(const char* pin, void (*callback)(float x, float y)) {
  if (_joystickCbCount >= MAX_SUBSCRIPTIONS) {
    TL_LOG("[MQTT] ✗ Subscription limit reached");
    return;
  }
  JoystickCallback& cb = _joystickCbs[_joystickCbCount++];
  strncpy(cb.pin, pin, sizeof(cb.pin) - 1);
  cb.pin[sizeof(cb.pin) - 1] = '\0';
  cb.fn = callback;

  if (_mqtt.connected()) {
    String topic = buildTopic("Joystick", pin);
    bool ok = _mqtt.subscribe(topic.c_str(), 1);
    TL_LOG(ok ? "[MQTT] ✓ Subscribed: " + topic : "[MQTT] ✗ Subscribe failed: " + topic);
  }
}

void publishMQTT(const char* widgetType, const char* pin, const TLValue& value) {
  // Offline with storage switched off: the reading is dropped, as before.
  // Reconnection is handled by checkConnections().
  if (!_mqtt.connected() && !tlOfflineEnabled()) return;

  // Build standard ThingsLinker payload: {"v": <typed value>, "t": <unix_timestamp>}
  char payload[TL_PAYLOAD_BUFFER_SIZE];
  if (tlEncodePayload(value, getTimestamp(), payload, sizeof(payload)) == 0) {
    TL_LOG("[MQTT] ✗ Value too long (" + String(pin) + ") — not sent");
    return;
  }

  bool waiting = !_mqtt.connected() || tlOfflineCount() > 0;
  if (sendOrStore(widgetType, pin, payload) && !waiting && tlOfflineCount() == 0) {
    TL_LOG("[MQTT] ✓ " + String(pin) + " = " + value.toString());
  }
}

void publishMQTTMap(const char* pin, float lat, float lng) {
  if (!_mqtt.connected() && !tlOfflineEnabled()) return;

  // App parseLoc() looks for "lat" and "lng" fields in the payload object.
  // Include "v" as well (= latitude) to stay compatible with the standard payload schema.
  JsonDocument doc;
  doc["v"]   = lat;
  doc["lat"] = lat;
  doc["lng"] = lng;
  doc["t"]   = getTimestamp();

  char payload[96];
  serializeJson(doc, payload, sizeof(payload));

  bool waiting = !_mqtt.connected() || tlOfflineCount() > 0;
  if (sendOrStore("Map", pin, payload) && !waiting && tlOfflineCount() == 0) {
    TL_LOG("[MQTT] ✓ Map " + String(pin) + " lat=" + String(lat) + " lng=" + String(lng));
  }
}

void loopMQTT() {
  if (_mqtt.connected()) {
    _mqtt.loop();
    sendStoredReadings();

    // Repeat the retained ONLINE now and then (see TL_STATUS_REFRESH_MS)
    if (millis() - _statusSentAt >= TL_STATUS_REFRESH_MS) {
      if (_mqtt.publish(statusTopic().c_str(), "ONLINE", true)) _statusSentAt = millis();
    }
  }
  // Note: reconnection is handled externally by ThingsLinker::checkConnections()
}

String getChipID() {
#ifdef ESP32
  uint64_t mac = ESP.getEfuseMac();
  char buf[13];
  snprintf(buf, sizeof(buf), "%04X%08X",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
#elif defined(ESP8266)
  return String(ESP.getChipId(), HEX);
#else
  return "UNKNOWN";
#endif
}
