/**
 * @file ThingsLinkerGSM.cpp
 * @brief ThingsLinker GSM variant — Simcom A7672 / SIM7672 native AT implementation
 *
 * Uses A76XX native MQTT AT commands (AT+CMQTT*) directly over HardwareSerial.
 * No TinyGSM, no PubSubClient — zero external MQTT dependencies.
 *
 * Connection flow:
 *   Power modem → AT ready → configure SSL → network registration →
 *   GPRS data context → AT+CMQTTSTART → AT+CMQTTACCQ → AT+CMQTTSSLCFG →
 *   AT+CMQTTCONNECT → AT+CMQTTSUB (per widget) → loop: AT+CMQTTPUB
 */

#include "ThingsLinkerGSM.h"
#include "TL_Codec.h"
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ThingsLinkerGSM::ThingsLinkerGSM(HardwareSerial& serial,
                                  const char*     authToken,
                                  const char*     blueprintId)
  : _serial(serial),
    _authToken(authToken),
    _blueprintId(blueprintId),
    _clientKey(nullptr),
    _secretKey(nullptr),
    _apn(""),
    _gprsUser(""),
    _gprsPass(""),
    _pwrPin(-1),
    _initialized(false),
    _mqttOk(false),
    _networkOk(false),
    _sigQuality(99),
    _lastCheck(0),
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
  _rxTopic[0]   = '\0';
  _rxPayload[0] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// begin()
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::begin(const char* clientKey,
                             const char* secretKey,
                             const char* apn,
                             int         pwrPin,
                             const char* gprsUser,
                             const char* gprsPass) {
  _clientKey = clientKey;
  _secretKey = secretKey;
  _apn       = apn;
  _pwrPin    = pwrPin;
  _gprsUser  = gprsUser;
  _gprsPass  = gprsPass;

  if (!Serial) { Serial.begin(115200); delay(100); }

  TL_LOG("\n========================================");
  TL_LOG("   ThingsLinker GSM Library v2.0");
  TL_LOG("   Simcom A7672 / SIM7672 (native AT)");
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

  char line[64];
  _sendAT("AT+CGMM");                 // model info
  if (_readLine(line, sizeof(line), 2000)) {
    TL_LOG("[GSM] Model: " + String(line));
    _readLine(line, sizeof(line), 1000); // consume OK
  }

  // Step 4: SSL configuration (context 0, no cert verification)
  // AT+CSSLCFG="sslversion",<ctx>,4   → TLS 1.2
  // AT+CSSLCFG="authmode",<ctx>,0     → no certificate verification
  // AT+CSSLCFG="ignorelocaltime",<ctx>,1 → ignore RTC time mismatch
  _atCmd("AT+CSSLCFG=\"sslversion\",0,4");
  _atCmd("AT+CSSLCFG=\"authmode\",0,0");
  _atCmd("AT+CSSLCFG=\"ignorelocaltime\",0,1");

  // Step 5: network + MQTT
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
    char line[512];
    int pos = 0;
    uint32_t deadline = millis() + 50;
    while (millis() < deadline) {
      if (_serial.available()) {
        char c = (char)_serial.read();
        if (c == '\n') break;
        if (c != '\r' && pos < (int)sizeof(line) - 1) line[pos++] = c;
        deadline = millis() + 20;  // reset timeout on each char
      }
    }
    line[pos] = '\0';
    if (pos > 0) _processLine(line);
  }

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
}

// ─────────────────────────────────────────────────────────────────────────────
// AT command layer
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_sendAT(const char* cmd) {
  while (_serial.available()) _serial.read();  // flush
  _serial.print(cmd);
  _serial.print("\r\n");
}

bool ThingsLinkerGSM::_readLine(char* buf, int len, uint32_t timeoutMs) {
  uint32_t deadline = millis() + timeoutMs;
  int pos = 0;
  while (millis() < deadline) {
    if (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '\n') {
        buf[pos] = '\0';
        // strip trailing \r
        if (pos > 0 && buf[pos-1] == '\r') buf[--pos] = '\0';
        return true;
      }
      if (c != '\r' && pos < len - 1) buf[pos++] = c;
    }
  }
  buf[pos] = '\0';
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

  // Wait for ">" prompt
  uint32_t deadline = millis() + timeoutMs;
  bool gotPrompt = false;
  while (millis() < deadline) {
    if (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '>') { gotPrompt = true; break; }
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
  TL_LOG("[GSM] Power-key pulse on GPIO" + String(_pwrPin));
  pinMode(_pwrPin, OUTPUT);
  digitalWrite(_pwrPin, LOW);
  delay(200);
  digitalWrite(_pwrPin, HIGH);
  delay(1500);
  digitalWrite(_pwrPin, LOW);
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

  // Wait for CGREG=1 (home) or 5 (roaming)
  uint32_t deadline = millis() + 60000UL;
  bool registered = false;
  while (millis() < deadline) {
    _sendAT("AT+CGREG?");
    char line[64];
    // Read lines until OK, looking for +CGREG: n,m
    while (_readLine(line, sizeof(line), 2000)) {
      if (strncmp(line, "+CGREG:", 7) == 0) {
        // Format: +CGREG: <n>,<stat>
        int n = 0, stat = 0;
        sscanf(line + 7, " %d,%d", &n, &stat);
        if (stat == 1 || stat == 5) { registered = true; }
        break;
      }
      if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
    }
    if (registered) break;
    delay(2000);
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

  // Configure PDP context (cid=1)
  {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", _apn);
    _atCmd(cmd);
  }

  // Activate PDP context
  TL_LOGF("[GSM] Activating GPRS (APN: %s)...\n", _apn);
  if (!_atCmd("AT+CGACT=1,1", 15000)) {
    // May already be active — check
    _sendAT("AT+CGACT?");
    bool active = false;
    while (_readLine(line, sizeof(line), 3000)) {
      if (strstr(line, "+CGACT: 1,1")) { active = true; }
      if (strncmp(line, "OK", 2) == 0) break;
    }
    if (!active) {
      TL_LOG("[GSM] GPRS activation failed — check APN, SIM data plan");
      return false;
    }
  }

  // Read assigned IP
  _sendAT("AT+CGPADDR=1");
  while (_readLine(line, sizeof(line), 3000)) {
    if (strncmp(line, "+CGPADDR:", 9) == 0) {
      // Format: +CGPADDR: 1,<ip>
      char* comma = strchr(line + 9, ',');
      if (comma) {
        _ipAddress = String(comma + 1);
        _ipAddress.trim();
        // Strip surrounding quotes if present
        _ipAddress.replace("\"", "");
      }
      break;
    }
    if (strncmp(line, "OK", 2) == 0) break;
  }

  TL_LOG("[GSM] GPRS connected — IP: " + _ipAddress);
  _networkOk = true;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT connection using native A76XX AT+CMQTT* commands
//
// Sequence:
//   AT+CMQTTSTART                             — start MQTT service (PDP ctx)
//   AT+CMQTTACCQ=0,"<clientId>",1             — acquire client, type=1 (SSL)
//   AT+CMQTTSSLCFG=0,0                        — link session 0 → SSL ctx 0
//   AT+CMQTTWILLTOPIC=0,<len>  +data          — set LWT topic
//   AT+CMQTTWILLMSG=0,<len>,1  +data          — set LWT message (QoS 1)
//   AT+CMQTTCONNECT=0,"ssl://host:port",60,1,"user","pass" — connect
//   → URC: +CMQTTCONNECT: 0,0  means success
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_connectMQTT() {
  TL_LOG("[MQTT] Starting MQTT service...");

  // Stop any previous session cleanly (ignore errors)
  _atCmd("AT+CMQTTDISC=0,10", 12000);
  delay(500);
  _atCmd("AT+CMQTTREL=0", 2000);
  delay(200);
  _atCmd("AT+CMQTTSTOP", 3000);
  delay(500);

  // Start MQTT service
  if (!_atCmd("AT+CMQTTSTART", 5000)) {
    // Already started? Try to continue
    TL_LOG("[MQTT] CMQTTSTART failed (may already be running)");
  }
  delay(200);

  // Build client ID and topics
  String clientId    = "TLG_" + getChipID();
  String statusTopic = "device/status/" + String(_blueprintId)
                       + "/" + String(_authToken) + "/";

  // Acquire client (index=0, type=1 for SSL)
  {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",1", clientId.c_str());
    if (!_atCmd(cmd, 3000)) {
      TL_LOG("[MQTT] CMQTTACCQ failed");
      return false;
    }
  }
  delay(100);

  // Link session 0 to SSL context 0
  _atCmd("AT+CMQTTSSLCFG=0,0", 2000);

  // Set LWT topic
  {
    int len = statusTopic.length();
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTWILLTOPIC=0,%d", len);
    _atDataCmd(cmd, statusTopic.c_str(), len, 3000);
  }

  // Set LWT message "OFFLINE", QoS 1, retain=1
  {
    const char* lwtMsg = "OFFLINE";
    int len = strlen(lwtMsg);
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTWILLMSG=0,%d,1", len);
    _atDataCmd(cmd, lwtMsg, len, 3000);
  }

  // Connect to broker
  // Format: AT+CMQTTCONNECT=<idx>,"<url>",<keepalive>,<cleanSession>[,"<user>","<pass>"]
  TL_LOG("[MQTT] Connecting to ssl://" MQTT_SERVER ":" + String(MQTT_PORT));
  {
    char cmd[256];
    bool hasAuth = (_clientKey && _clientKey[0]) && (_secretKey && _secretKey[0]);
    if (hasAuth) {
      snprintf(cmd, sizeof(cmd),
               "AT+CMQTTCONNECT=0,\"ssl://" MQTT_SERVER ":%d\",%d,1,\"%s\",\"%s\"",
               MQTT_PORT, MQTT_KEEPALIVE, _clientKey, _secretKey);
    } else {
      snprintf(cmd, sizeof(cmd),
               "AT+CMQTTCONNECT=0,\"ssl://" MQTT_SERVER ":%d\",%d,1",
               MQTT_PORT, MQTT_KEEPALIVE);
    }
    _sendAT(cmd);
  }

  // Wait for +CMQTTCONNECT: 0,<err> URC (up to 20 s)
  {
    char line[128];
    uint32_t deadline = millis() + 20000UL;
    bool connected = false;
    while (millis() < deadline) {
      if (_readLine(line, sizeof(line), deadline - millis())) {
        if (strncmp(line, "+CMQTTCONNECT:", 14) == 0) {
          // +CMQTTCONNECT: <idx>,<err>
          int idx = 0, err = -1;
          sscanf(line + 14, " %d,%d", &idx, &err);
          if (err == 0) {
            connected = true;
          } else {
            TL_LOGF("[MQTT] Connect error %d "
                    "(0=ok,3=sock fail,30=bad auth,32=handshake fail,33=no cert)\n", err);
          }
          break;
        }
        // Handle ERROR response too
        if (strncmp(line, "ERROR", 5) == 0) break;
      }
    }

    if (!connected) {
      TL_LOG("[MQTT] Connection failed");
      return false;
    }
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
    String statusTopic = "device/status/" + String(_blueprintId)
                         + "/" + String(_authToken) + "/";
    _mqttPublishAT(statusTopic.c_str(), "ONLINE");
    TL_LOG("[MQTT] Status → ONLINE");
  }

  // Re-subscribe all registered callbacks
  _subscribeAll();

  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-subscribe all registered callbacks (called after every MQTT connect)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_subscribeAll() {
  for (int i = 0; i < _boolCount; i++) {
    String t = _buildTopic(_boolCbs[i].widgetType, _boolCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
  }
  for (int i = 0; i < _floatCount; i++) {
    String t = _buildTopic(_floatCbs[i].widgetType, _floatCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
  }
  for (int i = 0; i < _textCount; i++) {
    String t = _buildTopic(_textCbs[i].widgetType, _textCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
  }
  for (int i = 0; i < _rgbCount; i++) {
    String t = _buildTopic("RGB", _rgbCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
  }
  for (int i = 0; i < _joyCount; i++) {
    String t = _buildTopic("Joystick", _joyCbs[i].pin);
    _mqttSubscribeAT(t.c_str());
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT publish via AT commands
//   AT+CMQTTTOPIC=0,<len>   → > → <topic>  → OK
//   AT+CMQTTPAYLOAD=0,<len> → > → <payload> → OK
//   AT+CMQTTPUB=0,1,60      → OK → +CMQTTPUB: 0,0
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_mqttPublishAT(const char* topic, const char* payload) {
  if (!_mqttOk) return false;

  int topicLen   = strlen(topic);
  int payloadLen = strlen(payload);

  // Set topic
  {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", topicLen);
    if (!_atDataCmd(cmd, topic, topicLen, 3000)) {
      TL_LOG("[MQTT] CMQTTTOPIC failed");
      return false;
    }
  }

  // Set payload
  {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", payloadLen);
    if (!_atDataCmd(cmd, payload, payloadLen, 3000)) {
      TL_LOG("[MQTT] CMQTTPAYLOAD failed");
      return false;
    }
  }

  // Publish (QoS 1, timeout 60 s)
  _sendAT("AT+CMQTTPUB=0,1,60");
  char line[64];
  uint32_t deadline = millis() + 10000UL;
  while (millis() < deadline) {
    if (_readLine(line, sizeof(line), deadline - millis())) {
      if (strncmp(line, "+CMQTTPUB:", 10) == 0) {
        int idx = 0, err = -1;
        sscanf(line + 10, " %d,%d", &idx, &err);
        if (err == 0) return true;
        TL_LOGF("[MQTT] Publish error %d\n", err);
        return false;
      }
      if (strncmp(line, "ERROR", 5) == 0) return false;
    }
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT subscribe via AT commands
//   AT+CMQTTSUBTOPIC=0,<len>,1  → > → <topic> → OK
//   AT+CMQTTSUB=0               → OK → +CMQTTSUB: 0,0
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_mqttSubscribeAT(const char* topic) {
  if (!_mqttOk) return false;

  int len = strlen(topic);
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%d,1", len);
  if (!_atDataCmd(cmd, topic, len, 3000)) {
    TL_LOG("[MQTT] CMQTTSUBTOPIC failed: " + String(topic));
    return false;
  }

  _sendAT("AT+CMQTTSUB=0");
  char line[64];
  uint32_t deadline = millis() + 5000UL;
  while (millis() < deadline) {
    if (_readLine(line, sizeof(line), deadline - millis())) {
      if (strncmp(line, "+CMQTTSUB:", 10) == 0) {
        int idx = 0, err = -1;
        sscanf(line + 10, " %d,%d", &idx, &err);
        bool ok = (err == 0);
        TL_LOGF("[MQTT] %s sub: %s\n", topic, ok ? "OK" : "FAIL");
        return ok;
      }
      if (strncmp(line, "ERROR", 5) == 0) return false;
    }
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// URC / line processor (called from run() for each line read)
//
// Incoming MQTT message sequence:
//   +CMQTTRXSTART: <idx>,<topic_len>,<payload_len>
//   +CMQTTRXTOPIC: <idx>,<sub_topic_len>
//   <topic data — sub_topic_len bytes, may contain \r\n>
//   +CMQTTRXPAYLOAD: <idx>,<sub_payload_len>
//   <payload data>
//   +CMQTTRXEND: <idx>
//
// Connection lost:
//   +CMQTTCONNLOST: <idx>,<cause>
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_processLine(const char* line) {
  // ── Incoming message start ──────────────────────────────────────────────────
  if (strncmp(line, "+CMQTTRXSTART:", 14) == 0) {
    int idx = 0, tLen = 0, pLen = 0;
    sscanf(line + 14, " %d,%d,%d", &idx, &tLen, &pLen);
    _rxTopicExpected   = tLen;
    _rxPayloadExpected = pLen;
    _rxTopic[0]        = '\0';
    _rxPayload[0]      = '\0';
    _rxState           = GSM_RX_IDLE;  // next line will be +CMQTTRXTOPIC
    return;
  }

  // ── Topic header ────────────────────────────────────────────────────────────
  if (strncmp(line, "+CMQTTRXTOPIC:", 14) == 0) {
    // Read exactly _rxTopicExpected bytes from serial
    if (_rxTopicExpected > 0 && _rxTopicExpected < (int)sizeof(_rxTopic)) {
      _readBytes(_rxTopic, _rxTopicExpected, 2000);
      _rxTopic[_rxTopicExpected] = '\0';
    }
    _rxState = GSM_RX_TOPIC;
    return;
  }

  // ── Payload header ──────────────────────────────────────────────────────────
  if (strncmp(line, "+CMQTTRXPAYLOAD:", 16) == 0) {
    // Read exactly _rxPayloadExpected bytes from serial
    if (_rxPayloadExpected > 0 && _rxPayloadExpected < (int)sizeof(_rxPayload)) {
      _readBytes(_rxPayload, _rxPayloadExpected, 3000);
      _rxPayload[_rxPayloadExpected] = '\0';
    }
    _rxState = GSM_RX_PAYLOAD;
    return;
  }

  // ── End of message — dispatch ────────────────────────────────────────────────
  if (strncmp(line, "+CMQTTRXEND:", 12) == 0) {
    if (_rxTopic[0] && _rxPayload[0]) {
      TL_LOG("[MQTT] RX topic:   " + String(_rxTopic));
      TL_LOG("[MQTT] RX payload: " + String(_rxPayload));
      _dispatchMessage(_rxTopic, _rxPayload);
    }
    _rxTopic[0]   = '\0';
    _rxPayload[0] = '\0';
    _rxState      = GSM_RX_IDLE;
    return;
  }

  // ── Connection lost ──────────────────────────────────────────────────────────
  if (strncmp(line, "+CMQTTCONNLOST:", 15) == 0) {
    int idx = 0, cause = 0;
    sscanf(line + 15, " %d,%d", &idx, &cause);
    TL_LOGF("[MQTT] Connection lost (cause=%d) — will reconnect\n", cause);
    _mqttOk = false;
    return;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch a fully received MQTT message to registered callbacks
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_dispatchMessage(const char* topic, const char* payload) {
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

  TL_LOGF("[MQTT] pin=%s  value=%.2f\n", pin, value);

  // RGB widget
  if (!doc["r"].isNull() && !doc["g"].isNull() && !doc["b"].isNull()) {
    for (int i = 0; i < _rgbCount; i++) {
      if (strcmp(pin, _rgbCbs[i].pin) == 0 && _rgbCbs[i].fn) {
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
      if (strcmp(pin, _joyCbs[i].pin) == 0 && _joyCbs[i].fn) {
        _joyCbs[i].fn(doc["x"].as<float>(), doc["y"].as<float>());
        return;
      }
    }
  }

  // Text callbacks (Terminal)
  for (int i = 0; i < _textCount; i++) {
    if (strcmp(pin, _textCbs[i].pin) == 0 && _textCbs[i].fn) {
      char scratch[32];
      _textCbs[i].fn(tlTextOf(doc, scratch, sizeof(scratch)));
      return;
    }
  }

  // Bool callbacks (Button / LED / Switch)
  for (int i = 0; i < _boolCount; i++) {
    if (strcmp(pin, _boolCbs[i].pin) == 0 && _boolCbs[i].fn) {
      _boolCbs[i].fn(tlBoolOf(doc));
      return;
    }
  }

  // Float callbacks (Slider, Timer, Value Display)
  for (int i = 0; i < _floatCount; i++) {
    if (strcmp(pin, _floatCbs[i].pin) == 0 && _floatCbs[i].fn) {
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
void ThingsLinkerGSM::_syncTime() {
  _atCmd("AT+CTZU=1");   // enable automatic NITZ time update
  delay(1000);

  _sendAT("AT+CCLK?");
  char line[64];
  String timeStr;
  while (_readLine(line, sizeof(line), 2000)) {
    if (strncmp(line, "+CCLK:", 6) == 0) {
      // Format: +CCLK: "YY/MM/DD,HH:MM:SS±QQ"
      timeStr = String(line + 7);
      timeStr.replace("\"", "");
      timeStr.trim();
      break;
    }
    if (strncmp(line, "OK", 2) == 0) break;
  }

  if (timeStr.length() < 17) {
    TL_LOG("[Time] NITZ unavailable — using millis() fallback");
    return;
  }

  TL_LOG("[Time] Modem clock: " + timeStr);

  int yr = timeStr.substring(0, 2).toInt() + 2000;
  int mo = timeStr.substring(3, 5).toInt();
  int dy = timeStr.substring(6, 8).toInt();
  int hr = timeStr.substring(9, 11).toInt();
  int mn = timeStr.substring(12, 14).toInt();
  int sc = timeStr.substring(15, 17).toInt();

  int tzSign = (timeStr.length() > 17 && timeStr[17] == '-') ? -1 : 1;
  int tzQtr  = (timeStr.length() > 18) ? timeStr.substring(18).toInt() : 0;
  int tzSec  = tzSign * tzQtr * 15 * 60;

  struct tm tmv = {};
  tmv.tm_year  = yr - 1900;
  tmv.tm_mon   = mo - 1;
  tmv.tm_mday  = dy;
  tmv.tm_hour  = hr;
  tmv.tm_min   = mn;
  tmv.tm_sec   = sc;
  tmv.tm_isdst = 0;

  time_t epoch = mktime(&tmv) - tzSec;

  if (epoch > (time_t)NTP_VALID_EPOCH) {
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
    TL_LOGF("[Time] UTC epoch set: %ld\n", (long)epoch);
  } else {
    TL_LOG("[Time] Epoch < 2020 — NITZ may not be available on this carrier");
  }
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
    if (_mqttPublishAT(topic.c_str(), payload)) return true;
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
void ThingsLinkerGSM::terminal(const char* p, const TLValue& v) { _publish("Value Display", p, v); }
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
// GPS — A7672 built-in GNSS engine
// ─────────────────────────────────────────────────────────────────────────────

void ThingsLinkerGSM::startGPS() {
  // Power on the GNSS module and select GPS + GLONASS mode
  _atCmd("AT+CGNSSPWR=1", 3000);
  delay(300);
  _atCmd("AT+CGNSSMODE=1", 2000);   // 1 = GPS + GLONASS
  _gpsStarted = true;
  TL_LOG("[GPS] GNSS engine started — waiting for first fix...");
}

// Parse +CGPSINFO response line.
// Format: +CGPSINFO: <lat>,<N/S>,<lon>,<E/W>,<date>,<time>,<alt>,<speed>,<course>
// No-fix:  +CGPSINFO: ,,,,,,,,
// Returns true if fix is valid.
bool ThingsLinkerGSM::_parseGpsInfo(const char* line) {
  const char* p = strstr(line, "+CGPSINFO:");
  if (!p) return false;
  p += 10;
  while (*p == ' ') p++;

  // Empty first field means no fix
  if (*p == ',') {
    _gpsValid = false;
    return false;
  }

  char latStr[16] = {}, ns[4] = {}, lonStr[16] = {}, ew[4] = {};
  char dateStr[8] = {}, timeStr[12] = {};
  float alt = 0, speedKnots = 0, course = 0;

  int n = sscanf(p, "%15[^,],%3[^,],%15[^,],%3[^,],%7[^,],%11[^,],%f,%f,%f",
                 latStr, ns, lonStr, ew, dateStr, timeStr,
                 &alt, &speedKnots, &course);
  if (n < 4) { _gpsValid = false; return false; }

  // DDMM.MMMM → decimal degrees
  float latRaw = atof(latStr);
  int   latDeg = (int)(latRaw / 100);
  _gpsLat = (latDeg + (latRaw - latDeg * 100.0f) / 60.0f) * (ns[0] == 'S' ? -1.0f : 1.0f);

  float lonRaw = atof(lonStr);
  int   lonDeg = (int)(lonRaw / 100);
  _gpsLng = (lonDeg + (lonRaw - lonDeg * 100.0f) / 60.0f) * (ew[0] == 'W' ? -1.0f : 1.0f);

  _gpsAltM     = alt;
  _gpsSpeedKmh = speedKnots * 1.852f;
  _gpsValid    = true;
  return true;
}

void ThingsLinkerGSM::_updateGPS() {
  // Send AT+CGPSINFO and read response
  while (_serial.available()) _serial.read();  // flush
  _serial.print("AT+CGPSINFO\r\n");

  char line[128];
  uint32_t deadline = millis() + 500;   // A7672 responds within ~100 ms
  int pos = 0;

  while (millis() < deadline) {
    if (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '\n') {
        line[pos] = '\0';
        if (pos > 0 && strstr(line, "+CGPSINFO:")) {
          _parseGpsInfo(line);
        }
        pos = 0;
      } else if (c != '\r' && pos < (int)sizeof(line) - 1) {
        line[pos++] = c;
      }
    }
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

void ThingsLinkerGSM::debug(bool enable) {
  _tlDebugEnabled = enable;
}
