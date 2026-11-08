/**
 * @file ThingsLinkerGSM_SIMCom.cpp
 * @brief ThingsLinkerGSM — the parts that use SIMCom A76XX commands
 *
 * SIMCom A7672 / SIM7672 (A76XX series), native MQTT AT commands (AT+CMQTT*).
 * Everything the modems share is in ThingsLinkerGSM.cpp.
 *
 * Connection flow:
 *   AT+CGDCONT → AT+CGACT → AT+CSSLCFG (TLS) → AT+CMQTTSTART → AT+CMQTTACCQ →
 *   AT+CMQTTSSLCFG → AT+CMQTTWILLTOPIC/WILLMSG → AT+CMQTTCONNECT →
 *   AT+CMQTTSUB (per widget) → loop: AT+CMQTTPUB
 */

#include "ThingsLinkerGSM.h"

// ─────────────────────────────────────────────────────────────────────────────
// Data connection: PDP context 1 with the APN
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_simcomActivateData() {
  char line[64];

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
//   AT+CMQTTCONNECT=0,"tcp://host:port",60,1,"user","pass" — connect
//   → URC: +CMQTTCONNECT: 0,0  means success
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_simcomOpenMQTT(const String& statusTopic) {
  // The modem keeps its own connection to the broker, so a session from before
  // the ESP32 restarted may still be open. Closing it below is a clean
  // disconnect, which does not send the last will: say OFFLINE first, or the
  // retained ONLINE would stay if the new connection then fails.
  if (_simcomSessionOpen()) {
    bool wasOk = _mqttOk;
    _mqttOk = true;   // _mqttPublishAT() only publishes on a live session
    if (_mqttPublishAT(statusTopic.c_str(), "OFFLINE", true)) {
      TL_LOG("[MQTT] Status → OFFLINE (closing the previous session)");
    }
    _mqttOk = wasOk;
  }

  // Stop any previous session cleanly (ignore errors)
  _atCmd("AT+CMQTTDISC=0,10", 12000);
  delay(500);
  _atCmd("AT+CMQTTREL=0", 2000);
  delay(200);
  _atCmd("AT+CMQTTSTOP", 3000);
  delay(500);

  // TLS settings for SSL context 0. Set before every connection: straight
  // after power-on the modem answers AT but still refuses these, so setting
  // them once at start-up could leave them unset.
  // AT+CSSLCFG="sslversion",<ctx>,4      → any TLS version
  // AT+CSSLCFG="authmode",<ctx>,0        → no certificate verification
  // AT+CSSLCFG="ignorelocaltime",<ctx>,1 → ignore RTC time mismatch
  // AT+CSSLCFG="enableSNI",<ctx>,1       → send the server's name in the handshake;
  //   brokers that host several names on one address need it to pick the certificate
  const char* tlsSettings[] = {
    "AT+CSSLCFG=\"sslversion\",0,4",
    "AT+CSSLCFG=\"authmode\",0,0",
    "AT+CSSLCFG=\"ignorelocaltime\",0,1",
    "AT+CSSLCFG=\"enableSNI\",0,1",
  };
  for (const char* setting : tlsSettings) {
    // A modem that has just started may refuse the first tries
    for (int attempt = 0; attempt < 5 && !_atCmd(setting, 2000); attempt++) delay(1000);
  }

  // Start MQTT service
  if (!_atCmd("AT+CMQTTSTART", 5000)) {
    // Already started? Try to continue
    TL_LOG("[MQTT] CMQTTSTART failed (may already be running)");
  }
  delay(200);

  // Build client ID and topics
  String clientId    = "TLG_" + getChipID();

  // Acquire client (index=0, type=1 for SSL)
  {
    // Built as a String: the client ID holds the blueprint key and auth token,
    // too long for a small fixed buffer
    String cmd = "AT+CMQTTACCQ=0,\"" + clientId + "\",1";
    if (!_atCmd(cmd.c_str(), 3000)) {
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
  // The address always starts with tcp://. Whether the link is encrypted was
  // chosen above (AT+CMQTTACCQ ...,1); the modem rejects an ssl:// address.
  TL_LOG("[MQTT] Connecting to " MQTT_SERVER ":" + String(MQTT_PORT) + " (TLS)");
  {
    char cmd[256];
    bool hasAuth = (_mqttUsername && _mqttUsername[0]) && (_mqttPassword && _mqttPassword[0]);
    if (hasAuth) {
      snprintf(cmd, sizeof(cmd),
               "AT+CMQTTCONNECT=0,\"tcp://" MQTT_SERVER ":%d\",%d,1,\"%s\",\"%s\"",
               MQTT_PORT, MQTT_KEEPALIVE, _mqttUsername, _mqttPassword);
    } else {
      snprintf(cmd, sizeof(cmd),
               "AT+CMQTTCONNECT=0,\"tcp://" MQTT_SERVER ":%d\",%d,1",
               MQTT_PORT, MQTT_KEEPALIVE);
    }
    _sendAT(cmd);
  }

  // Wait for +CMQTTCONNECT: 0,<err> URC (up to 20 s)
  char line[128];
  uint32_t deadline = millis() + 20000UL;
  while (millis() < deadline) {
    if (_readLine(line, sizeof(line), deadline - millis())) {
      if (strncmp(line, "+CMQTTCONNECT:", 14) == 0) {
        // +CMQTTCONNECT: <idx>,<err>
        int idx = 0, err = -1;
        sscanf(line + 14, " %d,%d", &idx, &err);
        if (err == 0) return true;
        TL_LOGF("[MQTT] Connect error %d "
                "(0=ok,3=sock fail,30=bad auth,32=handshake fail,33=no cert)\n", err);
        if (err == 32) {
          TL_LOG("[MQTT] The secure handshake failed. Some modem firmware cannot use a "
                 "server certificate of the ECDSA kind; the server then needs an RSA certificate.");
        }
        return false;
      }
      // The modem refused the command itself
      if (strncmp(line, "ERROR", 5) == 0) {
        TL_LOG("[MQTT] The modem rejected the connect command");
        return false;
      }
    }
  }
  return false;
}

// Whether MQTT session 0 in the modem is connected to the broker.
//   AT+CMQTTDISC?  → +CMQTTDISC: 0,0   (0 = connected, 1 = not connected)
bool ThingsLinkerGSM::_simcomSessionOpen() {
  _sendAT("AT+CMQTTDISC?");
  char line[48];
  bool open = false;
  uint32_t deadline = millis() + 2000UL;
  while (millis() < deadline && _readLine(line, sizeof(line), deadline - millis())) {
    int idx = -1, state = -1;
    if (sscanf(line, "+CMQTTDISC: %d,%d", &idx, &state) == 2 && idx == 0) open = (state == 0);
    if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
  }
  return open;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT publish via AT commands
//   AT+CMQTTTOPIC=0,<len>   → > → <topic>  → OK
//   AT+CMQTTPAYLOAD=0,<len> → > → <payload> → OK
//   AT+CMQTTPUB=0,1,60[,1]  → OK → +CMQTTPUB: 0,0     (,1 = retained)
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_simcomPublish(const char* topic, const char* payload, bool retain) {
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

  // Publish (QoS 1, timeout 60 s). The retain flag is off unless asked for.
  _sendAT(retain ? "AT+CMQTTPUB=0,1,60,1" : "AT+CMQTTPUB=0,1,60");
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
bool ThingsLinkerGSM::_simcomSubscribe(const char* topic) {
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
// Incoming message and lost-connection lines
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
void ThingsLinkerGSM::_simcomProcessLine(const char* line) {
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

  // ── End of message — queue it for run() ─────────────────────────────────────
  if (strncmp(line, "+CMQTTRXEND:", 12) == 0) {
    if (_rxTopic[0] && _rxPayload[0]) _queueMessage(_rxTopic, _rxPayload);
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
// Time from an internet time server. The modem sets its clock in UTC (zone 0).
// Returns 0 when the clock was updated.
// ─────────────────────────────────────────────────────────────────────────────
int ThingsLinkerGSM::_simcomSyncTime() {
  _atCmd("AT+CNTP=\"" NTP_SERVER_1 "\",0", 3000);
  _sendAT("AT+CNTP");
  char line[64];
  uint32_t deadline = millis() + 15000UL;
  while (millis() < deadline) {
    if (!_readLine(line, sizeof(line), deadline - millis())) break;
    if (strncmp(line, "+CNTP:", 6) == 0) return atoi(line + 6);   // +CNTP: 0 = clock updated
    if (strncmp(line, "ERROR", 5) == 0) break;
  }
  return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPS — A7672 built-in GNSS engine
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_simcomStartGPS() {
  // Power on the GNSS module and select GPS + GLONASS mode
  _atCmd("AT+CGNSSPWR=1", 3000);
  delay(300);
  _atCmd("AT+CGNSSMODE=1", 2000);   // 1 = GPS + GLONASS
}

// Parse +CGPSINFO response line.
// Format: +CGPSINFO: <lat>,<N/S>,<lon>,<E/W>,<date>,<time>,<alt>,<speed>,<course>
// No-fix:  +CGPSINFO: ,,,,,,,,
// Returns true if fix is valid.
bool ThingsLinkerGSM::_simcomParseGps(const char* line) {
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
