/**
 * @file ThingsLinkerGSM.cpp
 * @brief ThingsLinker GSM variant — the parts every supported modem shares
 *
 * Talks to the modem's own MQTT AT commands directly over HardwareSerial.
 * No TinyGSM, no PubSubClient — zero external MQTT dependencies.
 *
 * Connection flow:
 *   Power modem → AT ready → find the modem (AT+CGMI) → network registration →
 *   data context → MQTT (TLS) → subscribe per widget → loop: publish
 *
 * The steps that use one maker's own commands are in
 * ThingsLinkerGSM_SIMCom.cpp (A7672) and ThingsLinkerGSM_Quectel.cpp (EC200A).
 */

#include "ThingsLinkerGSM.h"
#include "TL_Echo.h"
#include "TL_Codec.h"
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ThingsLinkerGSM::ThingsLinkerGSM(HardwareSerial& serial,
                                  const char*     authToken,
                                  const char*     blueprintId)
  : _serial(serial),
    _modem(TL_MODEM_AUTO),
    _msgId(0),
    _authToken(authToken),
    _blueprintId(blueprintId),
    _mqttUsername(nullptr),
    _mqttPassword(nullptr),
    _apn(""),
    _gprsUser(""),
    _gprsPass(""),
    _pwrPin(-1),
    _initialized(false),
    _mqttOk(false),
    _networkOk(false),
    _sigQuality(99),
    _lastCheck(0),
    _pwrKeyActiveLow(false),
    _mqttConnectedAt(0),
    _lastStoredSend(0),
    _floatCount(0),
    _boolCount(0),
    _rgbCount(0),
    _joyCount(0),
    _textCount(0),
    _rxState(GSM_RX_IDLE),
    _rxTopicExpected(0),
    _rxPayloadExpected(0),
    _gpsStarted(false),
    _gpsValid(false),
    _gpsLat(0),
    _gpsLng(0),
    _gpsSpeedKmh(0),
    _gpsAltM(0),
    _lastGpsUpdate(0)
{
  _lineBuf[0]   = '\0';
  _rxTopic[0]   = '\0';
  _rxPayload[0] = '\0';
  _rxQueueHead  = 0;
  _rxQueueCount = 0;
  _statusSentAt = 0;
  _offlineSeen  = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// begin()
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::begin(const char* apn,
                             int         pwrPin,
                             const char* gprsUser,
                             const char* gprsPass) {
  begin("", "", apn, pwrPin, gprsUser, gprsPass);   // no MQTT login
}

void ThingsLinkerGSM::begin(const char* mqttUsername,
                             const char* mqttPassword,
                             const char* apn,
                             int         pwrPin,
                             const char* gprsUser,
                             const char* gprsPass) {
  _mqttUsername = mqttUsername;
  _mqttPassword = mqttPassword;
  _apn       = apn;
  _pwrPin    = pwrPin;
  _gprsUser  = gprsUser;
  _gprsPass  = gprsPass;

  if (!Serial) { Serial.begin(115200); delay(100); }

  TL_LOG("\n========================================");
  TL_LOG("   ThingsLinker GSM Library v2.2");
  TL_LOG("   4G modem: SIMCom A7672 / Quectel EC200A");
  TL_LOG("========================================");
  TL_LOG("Chip ID : " + getChipID());
  TL_LOG("Broker  : " MQTT_SERVER ":" + String(MQTT_PORT));
  TL_LOGF("APN     : %s\n", _apn);
  TL_LOG("========================================\n");

  // Step 1: power on modem
  _powerKey();

  // Step 2: wait for AT
  if (!_waitModem(30000)) {
    TL_LOG("[GSM] Modem not responding — check wiring and power supply");
    _initialized = true;
    return;
  }

  // Step 3: basic modem init
  _atCmd("ATE0");                     // echo off
  _atCmd("AT+CMEE=2");                // verbose error codes
  _detectModem();                     // which maker's commands to use

  char line[64];
  _sendAT("AT+CGMM");                 // model info
  while (_readLine(line, sizeof(line), 2000)) {
    if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
    if (line[0] != '\0') TL_LOG("[GSM] Model: " + String(line));
  }

  // Step 4: network + MQTT (the TLS settings are applied in _connectMQTT(),
  // when the modem has finished starting up)
  if (_connectNetwork()) {
    _syncTime();
    _connectMQTT();
  }

  _initialized = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// run()
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::run() {
  if (!_initialized) return;

  // Drain all available lines from the modem serial buffer
  while (_serial.available()) {
    int pos = 0;
    uint32_t deadline = millis() + 50;
    while (millis() < deadline) {
      if (_serial.available()) {
        char c = (char)_serial.read();
        if (c == '\n') break;
        if (c != '\r' && pos < TL_GSM_LINE_MAX - 1) _lineBuf[pos++] = c;
        deadline = millis() + 20;  // reset timeout on each char
      }
    }
    _lineBuf[pos] = '\0';
    if (pos > 0) _processLine(_lineBuf);
  }

  // Hand on messages that came in while a command was running
  _deliverQueued();

  // Poll GPS every 1 s (only when started)
  unsigned long now = millis();
  if (_gpsStarted && (now - _lastGpsUpdate >= 1000UL)) {
    _lastGpsUpdate = now;
    _updateGPS();
  }

  // Connection health check every 10 s
  if (now - _lastCheck >= 10000UL) {
    _lastCheck = now;
    _checkConnections();
  }

  // Readings stored while offline go out one at a time once connected
  _sendStoredReadings();

  // Answer an out-of-date OFFLINE
  if (_mqttOk && _offlineSeen && millis() - _statusSentAt >= TL_STATUS_ANSWER_GAP_MS) {
    String statusTopic = "device/status/" + String(_blueprintId) + "/" + String(_authToken) + "/";
    if (_mqttPublishAT(statusTopic.c_str(), "ONLINE", true)) {
      _statusSentAt = millis();
      _offlineSeen = false;
      TL_LOG("[MQTT] Status → ONLINE (replaced an old OFFLINE)");
    }
  }

  // Repeat the retained ONLINE now and then (see TL_STATUS_REFRESH_MS)
  if (_mqttOk && now - _statusSentAt >= TL_STATUS_REFRESH_MS) {
    String statusTopic = "device/status/" + String(_blueprintId) + "/" + String(_authToken) + "/";
    if (_mqttPublishAT(statusTopic.c_str(), "ONLINE", true)) _statusSentAt = millis();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// AT command layer
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_sendAT(const char* cmd) {
  // Clear old replies, but keep any incoming message waiting in the buffer
  char old[96];
  while (_serial.available()) _readLine(old, sizeof(old), 30);
  _serial.print(cmd);
  _serial.print("\r\n");
}

bool ThingsLinkerGSM::_readLine(char* buf, int len, uint32_t timeoutMs) {
  // The line is read whole into _lineBuf first: an incoming message can be
  // longer than the caller's buffer (Quectel sends one on a single line).
  uint32_t deadline = millis() + timeoutMs;
  int pos = 0;
  while (millis() < deadline) {
    if (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '\n') {
        _lineBuf[pos] = '\0';
        // An incoming message can arrive in the middle of any command's
        // reply. Take it in here and keep waiting for the reply itself.
        if (_isMessageLine(_lineBuf)) {
          _processLine(_lineBuf);
          pos = 0;
          continue;
        }
        strlcpy(buf, _lineBuf, len);
        return true;
      }
      if (c != '\r' && pos < TL_GSM_LINE_MAX - 1) _lineBuf[pos++] = c;
    }
  }
  _lineBuf[pos] = '\0';
  strlcpy(buf, _lineBuf, len);
  return false;
}

bool ThingsLinkerGSM::_readBytes(char* buf, int n, uint32_t timeoutMs) {
  uint32_t deadline = millis() + timeoutMs;
  int received = 0;
  while (received < n && millis() < deadline) {
    if (_serial.available()) {
      buf[received++] = (char)_serial.read();
    }
  }
  buf[received] = '\0';
  return (received == n);
}

bool ThingsLinkerGSM::_waitOK(uint32_t timeoutMs) {
  char line[64];
  uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    if (_readLine(line, sizeof(line), deadline - millis())) {
      if (strncmp(line, "OK", 2) == 0)    return true;
      if (strncmp(line, "ERROR", 5) == 0) return false;
      if (strstr(line, "+CME ERROR"))      return false;
      if (strstr(line, "+CMS ERROR"))      return false;
      // Otherwise: intermediate response line — keep reading
    }
  }
  return false;
}

bool ThingsLinkerGSM::_atCmd(const char* cmd, uint32_t timeoutMs) {
  _sendAT(cmd);
  bool ok = _waitOK(timeoutMs);
  TL_LOGF("[AT] %s → %s\n", cmd, ok ? "OK" : "FAIL");
  return ok;
}

// Send AT command → wait for ">" prompt → write data → wait for OK
bool ThingsLinkerGSM::_atDataCmd(const char* cmd, const char* data, int dataLen,
                                   uint32_t timeoutMs) {
  _sendAT(cmd);

  // Wait for ">" prompt. Whole lines seen before it may be incoming messages.
  uint32_t deadline = millis() + timeoutMs;
  bool gotPrompt = false;
  int  seenLen = 0;
  while (millis() < deadline) {
    if (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '>' && seenLen == 0) { gotPrompt = true; break; }
      if (c == '\n') {
        _lineBuf[seenLen] = '\0';
        if (_isMessageLine(_lineBuf)) _processLine(_lineBuf);
        seenLen = 0;
      } else if (c != '\r' && seenLen < TL_GSM_LINE_MAX - 1) {
        _lineBuf[seenLen++] = c;
      }
    }
  }
  if (!gotPrompt) {
    TL_LOGF("[AT] No > prompt for: %s\n", cmd);
    return false;
  }

  // Write data (no CRLF)
  _serial.write((const uint8_t*)data, dataLen);
  return _waitOK(timeoutMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Power key pulse (A7672: HIGH ≥1 s then LOW, wait ~5 s for boot)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_powerKey() {
  if (_pwrPin < 0) return;

  // A power-key press toggles the modem. If it is already on (for example the
  // ESP32 restarted but the modem kept its power), pressing would switch it off.
  if (_atCmd("AT", 500) || _atCmd("AT", 500)) {
    TL_LOG("[GSM] Modem is already on — power key not pressed");
    return;
  }

  TL_LOG("[GSM] Power-key pulse on GPIO" + String(_pwrPin));
  if (_pwrKeyActiveLow) {
    // The pin is wired straight to the modem's PWRKEY, which is pressed by
    // pulling it low. Afterwards the pin is released, not driven high: the
    // modem's key line runs at 1.8 V and has its own pull-up.
    pinMode(_pwrPin, OUTPUT);
    digitalWrite(_pwrPin, LOW);
    delay(1500);
    pinMode(_pwrPin, INPUT);
  } else {
    // The pin drives a transistor that pulls PWRKEY low while the pin is high.
    pinMode(_pwrPin, OUTPUT);
    digitalWrite(_pwrPin, LOW);
    delay(200);
    digitalWrite(_pwrPin, HIGH);
    delay(1500);
    digitalWrite(_pwrPin, LOW);
  }
  delay(5000);
  TL_LOG("[GSM] Power-key complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Wait for modem AT readiness
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_waitModem(uint32_t timeoutMs) {
  TL_LOG("[GSM] Waiting for modem AT response...");
  uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    if (_atCmd("AT", 1000)) {
      TL_LOG("[GSM] Modem ready");
      return true;
    }
    TL_LOGF("[GSM] Retrying... (%lu ms remaining)\n", deadline - millis());
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Network: register on cellular + activate GPRS PDP context
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_connectNetwork() {
  TL_LOG("[GSM] Waiting for network registration (up to 60 s)...");

  // Wait for registration: 1 (home) or 5 (roaming). +CGREG is the packet
  // network; an LTE-only modem may report it only in +CEREG, so both are asked.
  uint32_t deadline = millis() + 60000UL;
  bool registered = false;
  const char* queries[][2] = { { "AT+CGREG?", "+CGREG:" }, { "AT+CEREG?", "+CEREG:" } };
  while (millis() < deadline && !registered) {
    for (auto& q : queries) {
      _sendAT(q[0]);
      char line[64];
      size_t plen = strlen(q[1]);
      // Read lines until OK, looking for <prefix> n,stat
      while (_readLine(line, sizeof(line), 2000)) {
        if (strncmp(line, q[1], plen) == 0) {
          int n = 0, stat = 0;
          sscanf(line + plen, " %d,%d", &n, &stat);
          if (stat == 1 || stat == 5) { registered = true; }
          break;
        }
        if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
      }
      if (registered) break;
    }
    if (!registered) delay(2000);
  }

  if (!registered) {
    TL_LOG("[GSM] Network registration failed — check SIM, antenna, coverage");
    return false;
  }

  // Read signal quality
  _sendAT("AT+CSQ");
  char line[64];
  while (_readLine(line, sizeof(line), 2000)) {
    if (strncmp(line, "+CSQ:", 5) == 0) {
      sscanf(line + 5, " %d", &_sigQuality);
      break;
    }
    if (strncmp(line, "OK", 2) == 0) break;
  }
  TL_LOGF("[GSM] Registered — signal quality: %d/31\n", _sigQuality);

  // Data connection, with the modem's own commands
  bool dataOk = (_modem == TL_MODEM_QUECTEL) ? _quectelActivateData() : _simcomActivateData();
  if (!dataOk) return false;

  TL_LOG("[GSM] GPRS connected — IP: " + _ipAddress);
  _networkOk = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT connection. The modem's own commands open it (_simcomOpenMQTT() /
// _quectelOpenMQTT()); then ONLINE is published and every widget subscribed.
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_connectMQTT() {
  TL_LOG("[MQTT] Starting MQTT service...");

  String statusTopic = "device/status/" + String(_blueprintId)
                       + "/" + String(_authToken) + "/";

  bool connected = (_modem == TL_MODEM_QUECTEL) ? _quectelOpenMQTT(statusTopic)
                                                : _simcomOpenMQTT(statusTopic);
  if (!connected) {
    TL_LOG("[MQTT] Connection failed");
    return false;
  }

  TL_LOG("[MQTT] Connected!");

  // Mark connected BEFORE publish/subscribe (both check _mqttOk)
  _mqttOk = true;
  _mqttConnectedAt = millis();
  if (tlOfflineCount() > 0) {
    TL_LOGF("[Offline] Sending %u stored readings...\n", (unsigned)tlOfflineCount());
  }

  // Publish ONLINE status (retain, QoS 1)
  {
    // Retained, so an app that opens later still sees it. Without the flag
    // the retained "OFFLINE" from the last will would stay on the broker.
    _mqttPublishAT(statusTopic.c_str(), "ONLINE", true);
    _statusSentAt = millis();
    _offlineSeen  = false;
    TL_LOG("[MQTT] Status → ONLINE");
  }

  // Re-subscribe all registered callbacks
  _subscribeAll();

  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-subscribe all registered callbacks (called after every MQTT connect)
// ─────────────────────────────────────────────────────────────────────────────
// Each subscription can bring back one retained value from the broker. They are
// handed on between subscriptions, so a long list never overflows the queue.
void ThingsLinkerGSM::_subscribeAll() {
  // Follow our own status too, to answer an out-of-date OFFLINE (see _dispatchMessage)
  {
    String t = "device/status/" + String(_blueprintId) + "/" + String(_authToken) + "/";
    _mqttSubscribeAT(t.c_str());
    _deliverQueued();
  }
  for (int i = 0; i < _boolCount; i++) {
    String t = _buildTopic(_boolCbs[i].widgetType, _boolCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
    _deliverQueued();
  }
  for (int i = 0; i < _floatCount; i++) {
    String t = _buildTopic(_floatCbs[i].widgetType, _floatCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
    _deliverQueued();
  }
  for (int i = 0; i < _textCount; i++) {
    String t = _buildTopic(_textCbs[i].widgetType, _textCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
    _deliverQueued();
  }
  for (int i = 0; i < _rgbCount; i++) {
    String t = _buildTopic("RGB", _rgbCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
    _deliverQueued();
  }
  for (int i = 0; i < _joyCount; i++) {
    String t = _buildTopic("Joystick", _joyCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
    _deliverQueued();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT publish and subscribe, with the modem's own commands
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_mqttPublishAT(const char* topic, const char* payload, bool retain) {
  if (!_mqttOk) return false;
  return (_modem == TL_MODEM_QUECTEL) ? _quectelPublish(topic, payload, retain)
                                      : _simcomPublish(topic, payload, retain);
}

bool ThingsLinkerGSM::_mqttSubscribeAT(const char* topic) {
  if (!_mqttOk) return false;
  return (_modem == TL_MODEM_QUECTEL) ? _quectelSubscribe(topic) : _simcomSubscribe(topic);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lines that are not replies to a command: incoming messages and a lost
// connection (called from run() and from inside any command)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_processLine(const char* line) {
  if (_modem == TL_MODEM_QUECTEL) _quectelProcessLine(line);
  else                            _simcomProcessLine(line);
}

// Lines that belong to an incoming message or a lost connection, as opposed
// to a reply to the command being run
bool ThingsLinkerGSM::_isMessageLine(const char* line) {
  if (_modem == TL_MODEM_QUECTEL) {
    return strncmp(line, "+QMTRECV:", 9) == 0 || strncmp(line, "+QMTSTAT:", 9) == 0;
  }
  return strncmp(line, "+CMQTTRX", 8) == 0 || strncmp(line, "+CMQTTCONNLOST:", 15) == 0;
}

// Keep a received message for run(). When the queue is full the oldest goes.
void ThingsLinkerGSM::_queueMessage(const char* topic, const char* payload) {
  TL_LOG("[MQTT] RX topic:   " + String(topic));
  TL_LOG("[MQTT] RX payload: " + String(payload));
  if (_rxQueueCount == TL_GSM_RX_QUEUE) {
    TL_LOG("[MQTT] Too many messages waiting — oldest dropped");
    _rxQueueHead = (_rxQueueHead + 1) % TL_GSM_RX_QUEUE;
    _rxQueueCount--;
  }
  _GsmRxMsg& slot = _rxQueue[(_rxQueueHead + _rxQueueCount) % TL_GSM_RX_QUEUE];
  strlcpy(slot.topic, topic, sizeof(slot.topic));
  strlcpy(slot.payload, payload, sizeof(slot.payload));
  _rxQueueCount++;
}

// Hand queued messages to the callbacks, oldest first. A callback may publish,
// which can queue more messages; those go out on the same pass.
void ThingsLinkerGSM::_deliverQueued() {
  while (_rxQueueCount > 0) {
    _GsmRxMsg msg = _rxQueue[_rxQueueHead];
    _rxQueueHead = (_rxQueueHead + 1) % TL_GSM_RX_QUEUE;
    _rxQueueCount--;
    _dispatchMessage(msg.topic, msg.payload);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch a fully received MQTT message to registered callbacks
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_dispatchMessage(const char* topic, const char* payload) {
  // Our own status topic. An OFFLINE there while we are connected is out of
  // date: the last will of an earlier connection of this device (for example
  // a session the modem kept open across an ESP32 restart), delivered late.
  // Say ONLINE again so dashboards show the device as it is; loop() does that,
  // once for a burst (see TL_STATUS_ANSWER_GAP_MS).
  String statusTopic = "device/status/" + String(_blueprintId) + "/" + String(_authToken) + "/";
  if (statusTopic == topic) {
    if (strcmp(payload, "OFFLINE") == 0) _offlineSeen = true;
    return;
  }

  // The device's own message coming back is not a command from the app
  if (tlEchoIsOwn(topic, payload)) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    TL_LOG("[MQTT] JSON parse error");
    return;
  }
  if (doc["v"].isNull()) {
    TL_LOG("[MQTT] No 'v' field");
    return;
  }

  // "v" may be a number, a boolean or a string depending on the widget data type
  float value = tlNumberOf(doc);

  // Extract virtual pin from topic: device/{Type}/{Blueprint}/{AuthToken}/{Pin}/
  //   indices:                         0      1       2           3          4
  const char* p = topic;
  int slashes[5], found = 0;
  for (int i = 0; p[i] && found < 5; i++) {
    if (p[i] == '/') slashes[found++] = i;
  }
  if (found < 5) return;

  // pin = substring between slash[3]+1 and slash[4]
  int pinStart = slashes[3] + 1;
  int pinLen   = slashes[4] - pinStart;
  char pin[8];
  if (pinLen <= 0 || pinLen >= (int)sizeof(pin)) return;
  memcpy(pin, topic + pinStart, pinLen);
  pin[pinLen] = '\0';

  // The widget type as well as the pin decides the callback: several widgets
  // can share a pin (a Terminal and a Text Input on V2, for example)
  char type[20];
  int typeLen = slashes[1] - slashes[0] - 1;
  if (typeLen <= 0 || typeLen >= (int)sizeof(type)) return;
  memcpy(type, topic + slashes[0] + 1, typeLen);
  type[typeLen] = '\0';

  TL_LOGF("[MQTT] pin=%s  value=%.2f\n", pin, value);

  // RGB widget
  if (!doc["r"].isNull() && !doc["g"].isNull() && !doc["b"].isNull()) {
    for (int i = 0; i < _rgbCount; i++) {
      if (strcmp(type, "RGB") == 0 && strcmp(pin, _rgbCbs[i].pin) == 0 && _rgbCbs[i].fn) {
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

  // Joystick widget
  if (!doc["x"].isNull() && !doc["y"].isNull()) {
    for (int i = 0; i < _joyCount; i++) {
      if (strcmp(type, "Joystick") == 0 && strcmp(pin, _joyCbs[i].pin) == 0 && _joyCbs[i].fn) {
        _joyCbs[i].fn(doc["x"].as<float>(), doc["y"].as<float>());
        return;
      }
    }
  }

  // Text callbacks (Terminal)
  for (int i = 0; i < _textCount; i++) {
    if (strcmp(pin, _textCbs[i].pin) == 0 && strcmp(type, _textCbs[i].widgetType) == 0 && _textCbs[i].fn) {
      char scratch[32];
      _textCbs[i].fn(tlTextOf(doc, scratch, sizeof(scratch)));
      return;
    }
  }

  // Bool callbacks (Button / LED / Switch)
  for (int i = 0; i < _boolCount; i++) {
    if (strcmp(pin, _boolCbs[i].pin) == 0 && strcmp(type, _boolCbs[i].widgetType) == 0 && _boolCbs[i].fn) {
      _boolCbs[i].fn(tlBoolOf(doc));
      return;
    }
  }

  // Float callbacks (Slider, Timer, Value Display)
  for (int i = 0; i < _floatCount; i++) {
    if (strcmp(pin, _floatCbs[i].pin) == 0 && strcmp(type, _floatCbs[i].widgetType) == 0 && _floatCbs[i].fn) {
      _floatCbs[i].fn(value);
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection health check (called from run() every 10 s)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_checkConnections() {
  if (!_networkOk) {
    TL_LOG("[Check] Network lost — reconnecting...");
    if (_connectNetwork()) _syncTime();
  }

  if (_networkOk && !_mqttOk) {
    TL_LOG("[Check] MQTT lost — reconnecting...");
    if (!_connectMQTT()) {
      // MQTT failed — network context may have dropped; force full reconnect next time
      _networkOk = false;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Time sync via NITZ (AT+CTZU) + AT+CCLK
// ─────────────────────────────────────────────────────────────────────────────
// Reads the modem's clock ("YY/MM/DD,HH:MM:SS±QQ") and, when it is a real
// date, sets the ESP32's clock from it. A modem that has not been given the
// time counts from 1970 (some firmware shows that as year "70" or "80").
bool ThingsLinkerGSM::_clockFromModem() {
  _sendAT("AT+CCLK?");
  char line[64];
  String timeStr;
  while (_readLine(line, sizeof(line), 2000)) {
    if (strncmp(line, "+CCLK:", 6) == 0) {
      timeStr = String(line + 7);
      timeStr.replace("\"", "");
      timeStr.trim();
    }
    if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
  }
  if (timeStr.length() < 17) return false;

  TL_LOG("[Time] Modem clock: " + timeStr);

  int yy = timeStr.substring(0, 2).toInt();
  int mo = timeStr.substring(3, 5).toInt();
  int dy = timeStr.substring(6, 8).toInt();
  int hr = timeStr.substring(9, 11).toInt();
  int mn = timeStr.substring(12, 14).toInt();
  int sc = timeStr.substring(15, 17).toInt();

  // Two-digit years 24–69 are 2024–2069. Anything else is an unset clock.
  if (yy < 24 || yy > 69 || mo < 1 || mo > 12 || dy < 1 || dy > 31) return false;

  int tzSign = (timeStr.length() > 17 && timeStr[17] == '-') ? -1 : 1;
  int tzQtr  = (timeStr.length() > 18) ? timeStr.substring(18).toInt() : 0;
  long tzSec = (long)tzSign * tzQtr * 15 * 60;

  // Days since 1970-01-01 for a UTC date, worked out directly so the result
  // does not depend on the ESP32's own time-zone setting.
  int y = 2000 + yy;
  int m = mo;
  if (m <= 2) { y -= 1; m += 12; }
  long days = 365L * y + y / 4 - y / 100 + y / 400 + (153L * (m - 3) + 2) / 5 + dy - 719469L;
  time_t epoch = (time_t)days * 86400 + hr * 3600L + mn * 60L + sc - tzSec;

  if (epoch <= (time_t)NTP_VALID_EPOCH) return false;
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
  TL_LOGF("[Time] Clock set (UTC epoch %lu)\n", (unsigned long)epoch);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Time sync: an internet time server first, the modem's own clock as fallback
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_syncTime() {
  // Ask a time server and have the modem set its clock from it. The modem's
  // clock is not trusted as it stands: it may have been set earlier with
  // another zone but still be labelled "+00" (seen on an A7672: eight hours
  // ahead), which would put every reading hours in the future.
  bool ntpOk = false;
  // The first request right after the data connection comes up can fail, so
  // it is tried twice.
  for (int attempt = 1; attempt <= 2 && !ntpOk; attempt++) {
    int code = (_modem == TL_MODEM_QUECTEL) ? _quectelSyncTime() : _simcomSyncTime();
    ntpOk = (code == 0);
    if (!ntpOk) {
      TL_LOGF("[Time] Time server request %d failed (code %d)\n", attempt, code);
      delay(2000);
    }
  }

  if (ntpOk && _clockFromModem()) return;

  // No answer from the time server: fall back to the time the mobile network
  // itself provides, when it provides one.
  TL_LOG("[Time] Time server did not answer — trying the network's time");
  _atCmd("AT+CTZU=1");
  delay(1000);
  if (_clockFromModem()) return;

  TL_LOG("[Time] No time available yet — readings are timed from start-up until it is");
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish helpers
// ─────────────────────────────────────────────────────────────────────────────
unsigned long ThingsLinkerGSM::_getTimestamp() {
  time_t now = time(nullptr);
  return (now > (time_t)NTP_VALID_EPOCH) ? (unsigned long)now : (millis() / 1000UL);
}

String ThingsLinkerGSM::_buildTopic(const char* widgetType, const char* pin) {
  return String("device/") + widgetType + "/"
         + _blueprintId + "/" + _authToken + "/" + pin + "/";
}

/** True once the network has set the clock, i.e. _getTimestamp() is real time. */
static bool clockIsSet() {
  return time(nullptr) > (time_t)NTP_VALID_EPOCH;
}

/**
 * Send one reading now, or keep it for later when offline storage is on.
 * While stored readings are still waiting, a new one joins the end of the
 * queue, so everything arrives in the order it was taken.
 *
 * @return true when the reading was sent now
 */
bool ThingsLinkerGSM::_sendOrStore(const char* widgetType, const char* pin, const char* payload) {
  if (_mqttOk && tlOfflineCount() == 0) {
    String topic = _buildTopic(widgetType, pin);
    if (_mqttPublishAT(topic.c_str(), payload)) {
      tlEchoNote(topic.c_str(), payload);
      return true;
    }
  }

  if (tlOfflineEnabled()) {
    tlOfflineStore(widgetType, pin, payload, clockIsSet());
    TL_LOGF("[Offline] Stored %s (%u waiting)\n", pin, (unsigned)tlOfflineCount());
  }
  return false;
}

/** Send stored readings, oldest first, one per call. Called from run(). */
void ThingsLinkerGSM::_sendStoredReadings() {
  if (tlOfflineCount() == 0 || !_mqttOk) return;

  unsigned long now = millis();
  if (now - _lastStoredSend < TL_OFFLINE_SEND_INTERVAL_MS) return;

  // Readings taken before the clock was set need it to get their real time.
  // Give the network a moment after reconnecting; send anyway if it never comes.
  if (tlOfflineFrontNeedsClock() && !clockIsSet() &&
      now - _mqttConnectedAt < TL_OFFLINE_CLOCK_WAIT_MS) return;

  char widgetType[20];
  char pin[8];
  char payload[TL_PAYLOAD_BUFFER_SIZE];
  if (!tlOfflinePeek(widgetType, sizeof(widgetType), pin, sizeof(pin), payload, sizeof(payload),
                     clockIsSet() ? (unsigned long)time(nullptr) : 0UL)) {
    tlOfflinePop();   // unreadable record: drop it so the queue cannot stall
    return;
  }

  _lastStoredSend = now;
  String topic = _buildTopic(widgetType, pin);
  if (!_mqttPublishAT(topic.c_str(), payload)) return;   // try again next time
  tlEchoNote(topic.c_str(), payload);

  tlOfflinePop();
  if (tlOfflineCount() == 0) TL_LOG("[Offline] ✓ All stored readings sent");
}

void ThingsLinkerGSM::_publish(const char* widgetType, const char* pin, const TLValue& value) {
  char payload[TL_PAYLOAD_BUFFER_SIZE];
  if (tlEncodePayload(value, _getTimestamp(), payload, sizeof(payload)) == 0) {
    TL_LOGF("[MQTT] ✗ Value too long (%s) — not sent\n", pin);
    return;
  }

  bool ok = _sendOrStore(widgetType, pin, payload);
  // A stored reading is reported by _sendOrStore()
  if (ok || !tlOfflineEnabled()) {
    TL_LOGF("[MQTT] %s %s = %s\n", ok ? "✓" : "✗", pin, value.toString().c_str());
  }
}

void ThingsLinkerGSM::_publishMap(const char* pin, float lat, float lng) {
  JsonDocument doc;
  doc["v"]   = lat;
  doc["lat"] = lat;
  doc["lng"] = lng;
  doc["t"]   = _getTimestamp();

  char payload[96];
  serializeJson(doc, payload, sizeof(payload));

  _sendOrStore("Map", pin, payload);
}

void ThingsLinkerGSM::_subscribe(const char* widgetType, const char* pin) {
  if (!_mqttOk) return;  // will be called again in _subscribeAll() on reconnect
  String t = _buildTopic(widgetType, pin);
  _mqttSubscribeAT(t.c_str());
  _deliverQueued();
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish methods
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::button (const char* p, bool  v) { _publish("Button",       p, v); }
void ThingsLinkerGSM::led    (const char* p, bool  v) { _publish("LED",           p, v); }
void ThingsLinkerGSM::gauge  (const char* p, float v) { _publish("Gauge",         p, v); }
void ThingsLinkerGSM::chart  (const char* p, float v) { _publish("Chart",         p, v); }
void ThingsLinkerGSM::slider (const char* p, float v) { _publish("Slider",        p, v); }
void ThingsLinkerGSM::display (const char* p, const TLValue& v) { _publish("Value Display", p, v); }
void ThingsLinkerGSM::label   (const char* p, const TLValue& v) { _publish("Label",         p, v); }
void ThingsLinkerGSM::send    (const char* p, const TLValue& v) { _publish("Value Display", p, v); }
// Published on the display channel so the device's own Terminal subscription
// (onTerminal) does not receive it back. Apps match Terminal output by pin.
void ThingsLinkerGSM::terminal(const char* p, const TLValue& v) { _publish("Terminal",      p, v); }
void ThingsLinkerGSM::map    (const char* p, float lat, float lng) { _publishMap(p, lat, lng); }
void ThingsLinkerGSM::status     (const char* p, const TLValue& v) { _publish("Status",       p, v); }
void ThingsLinkerGSM::progress   (const char* p, float v)          { _publish("Progress Bar", p, v); }
void ThingsLinkerGSM::selector   (const char* p, const TLValue& v) { _publish("Selector",     p, v); }
void ThingsLinkerGSM::numberInput(const char* p, float v)          { _publish("Number Input", p, v); }
void ThingsLinkerGSM::publish(const char* widgetType, const char* p, const TLValue& v) { _publish(widgetType, p, v); }

// ─────────────────────────────────────────────────────────────────────────────
// Subscribe helpers
// ─────────────────────────────────────────────────────────────────────────────
static void _fillBoolCb(ThingsLinkerGSM::BoolCb* arr, int& count,
                         const char* pin, const char* type, void (*fn)(bool)) {
  strncpy(arr[count].pin,        pin,  sizeof(arr[count].pin)        - 1);
  strncpy(arr[count].widgetType, type, sizeof(arr[count].widgetType) - 1);
  arr[count].pin[sizeof(arr[count].pin) - 1]               = '\0';
  arr[count].widgetType[sizeof(arr[count].widgetType) - 1] = '\0';
  arr[count].fn = fn;
  count++;
}

static void _fillFloatCb(ThingsLinkerGSM::FloatCb* arr, int& count,
                          const char* pin, const char* type, void (*fn)(float)) {
  strncpy(arr[count].pin,        pin,  sizeof(arr[count].pin)        - 1);
  strncpy(arr[count].widgetType, type, sizeof(arr[count].widgetType) - 1);
  arr[count].pin[sizeof(arr[count].pin) - 1]               = '\0';
  arr[count].widgetType[sizeof(arr[count].widgetType) - 1] = '\0';
  arr[count].fn = fn;
  count++;
}

void ThingsLinkerGSM::onButton(const char* pin, void (*cb)(bool)) {
  if (_boolCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillBoolCb(_boolCbs, _boolCount, pin, "Button", cb);
  _subscribe("Button", pin);
}

void ThingsLinkerGSM::onLED(const char* pin, void (*cb)(bool)) {
  if (_boolCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillBoolCb(_boolCbs, _boolCount, pin, "LED", cb);
  _subscribe("LED", pin);
}

void ThingsLinkerGSM::onSwitch(const char* pin, void (*cb)(bool)) {
  if (_boolCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillBoolCb(_boolCbs, _boolCount, pin, "Switch", cb);
  _subscribe("Switch", pin);
}

void ThingsLinkerGSM::onSlider(const char* pin, void (*cb)(float)) {
  if (_floatCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillFloatCb(_floatCbs, _floatCount, pin, "Slider", cb);
  _subscribe("Slider", pin);
}

void ThingsLinkerGSM::onValue(const char* pin, void (*cb)(float)) {
  if (_floatCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillFloatCb(_floatCbs, _floatCount, pin, "Value Display", cb);
  _subscribe("Value Display", pin);
}

void ThingsLinkerGSM::onRGB(const char* pin,
                             void (*cb)(uint8_t, uint8_t, uint8_t, bool, uint16_t, const char*)) {
  if (_rgbCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  strncpy(_rgbCbs[_rgbCount].pin, pin, sizeof(_rgbCbs[_rgbCount].pin) - 1);
  _rgbCbs[_rgbCount].pin[sizeof(_rgbCbs[_rgbCount].pin) - 1] = '\0';
  _rgbCbs[_rgbCount].fn = cb;
  _rgbCount++;
  _subscribe("RGB", pin);
}

void ThingsLinkerGSM::onTerminal(const char* pin, void (*cb)(const char*)) {
  onWidgetText("Terminal", pin, cb);
}

void ThingsLinkerGSM::onWidget(const char* widgetType, const char* pin, void (*cb)(float)) {
  if (_floatCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillFloatCb(_floatCbs, _floatCount, pin, widgetType, cb);
  _subscribe(widgetType, pin);
}

void ThingsLinkerGSM::onWidgetText(const char* widgetType, const char* pin, void (*cb)(const char*)) {
  if (_textCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  TextCb& t = _textCbs[_textCount++];
  strncpy(t.pin,        pin,        sizeof(t.pin)        - 1);
  strncpy(t.widgetType, widgetType, sizeof(t.widgetType) - 1);
  t.pin[sizeof(t.pin) - 1]               = '\0';
  t.widgetType[sizeof(t.widgetType) - 1] = '\0';
  t.fn = cb;
  _subscribe(widgetType, pin);
}

void ThingsLinkerGSM::onSelector    (const char* pin, void (*cb)(float))       { onWidget("Selector",         pin, cb); }
void ThingsLinkerGSM::onSelectorText(const char* pin, void (*cb)(const char*)) { onWidgetText("Selector",     pin, cb); }
void ThingsLinkerGSM::onNumberInput (const char* pin, void (*cb)(float))       { onWidget("Number Input",     pin, cb); }
void ThingsLinkerGSM::onTextInput   (const char* pin, void (*cb)(const char*)) { onWidgetText("Text Input",   pin, cb); }
void ThingsLinkerGSM::onKeypad      (const char* pin, void (*cb)(float))       { onWidget("Keypad",           pin, cb); }
void ThingsLinkerGSM::onKeypadText  (const char* pin, void (*cb)(const char*)) { onWidgetText("Keypad",       pin, cb); }

void ThingsLinkerGSM::onTimer(const char* pin, void (*cb)(float)) {
  if (_floatCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  _fillFloatCb(_floatCbs, _floatCount, pin, "Timer", cb);
  _subscribe("Timer", pin);
}

void ThingsLinkerGSM::onJoystick(const char* pin, void (*cb)(float, float)) {
  if (_joyCount >= MAX_SUBSCRIPTIONS) { TL_LOG("[MQTT] Subscription limit reached"); return; }
  strncpy(_joyCbs[_joyCount].pin, pin, sizeof(_joyCbs[_joyCount].pin) - 1);
  _joyCbs[_joyCount].pin[sizeof(_joyCbs[_joyCount].pin) - 1] = '\0';
  _joyCbs[_joyCount].fn = cb;
  _joyCount++;
  _subscribe("Joystick", pin);
}

// ─────────────────────────────────────────────────────────────────────────────
// Status
// ─────────────────────────────────────────────────────────────────────────────
bool   ThingsLinkerGSM::networkConnected() { return _networkOk; }
bool   ThingsLinkerGSM::mqttConnected()    { return _mqttOk; }
String ThingsLinkerGSM::getIP()            { return _ipAddress; }
int    ThingsLinkerGSM::signalQuality()    { return _sigQuality; }

String ThingsLinkerGSM::getChipID() {
#ifdef ESP32
  uint64_t mac = ESP.getEfuseMac();
  char buf[13];
  snprintf(buf, sizeof(buf), "%04X%08X",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
#else
  return "UNKNOWN";
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Storage
// ─────────────────────────────────────────────────────────────────────────────
bool   ThingsLinkerGSM::saveString(const char* k, const String& v) { return ::saveString(k, v); }
String ThingsLinkerGSM::getString (const char* k, const String& d) { return ::getString(k, d);  }
bool   ThingsLinkerGSM::saveInt   (const char* k, int v)           { return ::saveInt(k, v);    }
int    ThingsLinkerGSM::getInt    (const char* k, int d)           { return ::getInt(k, d);     }
bool   ThingsLinkerGSM::saveFloat (const char* k, float v)         { return ::saveFloat(k, v);  }
float  ThingsLinkerGSM::getFloat  (const char* k, float d)         { return ::getFloat(k, d);   }
bool   ThingsLinkerGSM::saveBool  (const char* k, bool v)          { return ::saveBool(k, v);   }
bool   ThingsLinkerGSM::getBool   (const char* k, bool d)          { return ::getBool(k, d);    }
bool   ThingsLinkerGSM::hasKey    (const char* k)                  { return ::hasKey(k);        }
bool   ThingsLinkerGSM::removeKey (const char* k)                  { return ::removeKey(k);     }
void   ThingsLinkerGSM::clearAllData()                             { ::clearAllData();           }

// ─────────────────────────────────────────────────────────────────────────────
// GPS — the modem's built-in GNSS engine
// ─────────────────────────────────────────────────────────────────────────────

void ThingsLinkerGSM::startGPS() {
  if (_modem == TL_MODEM_QUECTEL) _quectelStartGPS();
  else                            _simcomStartGPS();
  _gpsStarted = true;
  TL_LOG("[GPS] GNSS engine started — waiting for first fix...");
}

// Ask for the position (called from run() every second while GPS is on)
//   SIMCom:  AT+CGPSINFO   → +CGPSINFO: ...
//   Quectel: AT+QGPSLOC=2  → +QGPSLOC: ...   (+CME ERROR: 516 = no fix yet)
void ThingsLinkerGSM::_updateGPS() {
  bool quectel = (_modem == TL_MODEM_QUECTEL);
  _sendAT(quectel ? "AT+QGPSLOC=2" : "AT+CGPSINFO");

  char line[128];
  uint32_t deadline = millis() + 500;   // the modem answers within ~100 ms
  while (millis() < deadline && _readLine(line, sizeof(line), deadline - millis())) {
    if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
    if (strstr(line, "+CME ERROR")) { _gpsValid = false; break; }
    if (quectel ? _quectelParseGps(line) : _simcomParseGps(line)) break;
  }
}

bool  ThingsLinkerGSM::gpsValid()    { return _gpsValid; }
float ThingsLinkerGSM::gpsLat()      { return _gpsLat; }
float ThingsLinkerGSM::gpsLng()      { return _gpsLng; }
float ThingsLinkerGSM::gpsSpeed()    { return _gpsSpeedKmh; }
float ThingsLinkerGSM::gpsAltitude() { return _gpsAltM; }

// ─────────────────────────────────────────────────────────────────────────────
// OTA
// ─────────────────────────────────────────────────────────────────────────────
OTAResult ThingsLinkerGSM::checkOTA(const char* apiServer) {
  return checkAndApplyOTA(_authToken, apiServer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::offlineStorage(bool enable, uint16_t maxRecords) {
  tlOfflineEnable(enable, maxRecords);
  TL_LOGF("[Offline] Storage %s\n", enable ? "on" : "off");
}
uint16_t ThingsLinkerGSM::offlineCount() { return tlOfflineCount(); }

void ThingsLinkerGSM::setPowerKeyActiveLow(bool activeLow) {
  _pwrKeyActiveLow = activeLow;
}

void ThingsLinkerGSM::setModem(TLModemType type) {
  _modem = type;
}

TLModemType ThingsLinkerGSM::modem() {
  return _modem;
}

// Ask the modem who made it, unless the sketch chose with setModem():
//   AT+CGMI → "SIMCOM INCORPORATED" / "Quectel"
void ThingsLinkerGSM::_detectModem() {
  if (_modem == TL_MODEM_AUTO) {
    _sendAT("AT+CGMI");
    char line[64];
    while (_readLine(line, sizeof(line), 2000)) {
      if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
      String maker(line);
      maker.toLowerCase();
      if (maker.indexOf("quectel") >= 0)     _modem = TL_MODEM_QUECTEL;
      else if (maker.indexOf("simcom") >= 0) _modem = TL_MODEM_SIMCOM;
    }
    if (_modem == TL_MODEM_AUTO) {
      TL_LOG("[GSM] Modem maker not recognised — using SIMCom commands "
             "(choose with iot.setModem() if this is wrong)");
      _modem = TL_MODEM_SIMCOM;
    }
  }
  TL_LOG(_modem == TL_MODEM_QUECTEL ? "[GSM] Modem: Quectel (EC200A commands)"
                                    : "[GSM] Modem: SIMCom (A7672 commands)");
}

void ThingsLinkerGSM::debug(bool enable) {
  _tlDebugEnabled = enable;
}
