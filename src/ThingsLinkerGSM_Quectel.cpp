/**
 * @file ThingsLinkerGSM_Quectel.cpp
 * @brief ThingsLinkerGSM — the parts that use Quectel EC200A commands
 *
 * Quectel EC200A (EC200x series), native MQTT AT commands (AT+QMT*).
 * Everything the modems share is in ThingsLinkerGSM.cpp.
 *
 * Connection flow:
 *   AT+QICSGP → AT+QIACT (data) → AT+QSSLCFG (TLS) → AT+QMTCFG (MQTT, last will) →
 *   AT+QMTOPEN → AT+QMTCONN → AT+QMTSUB (per widget) → loop: AT+QMTPUBEX
 *
 * Unlike the SIMCom modems, most results arrive as a separate line after OK
 * ("+QMTOPEN: 0,0"), and an incoming message is one line:
 *   +QMTRECV: <client>,<msgid>,"<topic>",<length>,"<payload>"
 */

#include "ThingsLinkerGSM.h"

// MQTT client 0 uses PDP context 1 and SSL context 0
// ─────────────────────────────────────────────────────────────────────────────

uint16_t ThingsLinkerGSM::_nextMsgId() {
  // 1–65535: a QoS 1 packet must not use id 0
  if (++_msgId == 0) _msgId = 1;
  return _msgId;
}

/**
 * Send a command, then wait for the line that carries its result
 * ("<prefix> <a>,<b>,..."), and read up to `count` numbers from it.
 * Returns false on ERROR or when the result line does not come in time.
 */
bool ThingsLinkerGSM::_quectelResult(const char* cmd, const char* prefix, uint32_t timeoutMs,
                                     int* values, int count) {
  _sendAT(cmd);
  size_t plen = strlen(prefix);
  char line[128];
  uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    if (!_readLine(line, sizeof(line), deadline - millis())) break;
    if (strncmp(line, prefix, plen) == 0) {
      const char* p = line + plen;
      for (int i = 0; i < count; i++) {
        while (*p == ' ' || *p == '"') p++;
        values[i] = atoi(p);
        p = strchr(p, ',');
        if (!p) break;
        p++;
      }
      return true;
    }
    if (strncmp(line, "ERROR", 5) == 0 || strstr(line, "+CME ERROR")) return false;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Data connection: context 1 with the APN, activated for the modem's own
// internet functions (MQTT uses them)
//   AT+QICSGP=1,1,"<apn>","<user>","<pass>",<auth>   → OK
//   AT+QIACT=1                                       → OK
//   AT+QIACT?  → +QIACT: 1,1,1,"10.12.34.56"         (context, state, type, IP)
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_quectelActivateData() {
  {
    bool login = (_gprsUser && _gprsUser[0]) || (_gprsPass && _gprsPass[0]);
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",%d",
             _apn, _gprsUser, _gprsPass, login ? 3 : 0);   // 3 = PAP or CHAP
    _atCmd(cmd);
  }

  TL_LOGF("[GSM] Activating data (APN: %s)...\n", _apn);
  _atCmd("AT+QIACT=1", 30000);   // fails when it is already active; checked below

  _sendAT("AT+QIACT?");
  char line[96];
  bool active = false;
  while (_readLine(line, sizeof(line), 3000)) {
    int cid = 0, state = 0;
    if (sscanf(line, "+QIACT: %d,%d", &cid, &state) == 2 && cid == 1 && state == 1) {
      active = true;
      const char* q = strchr(line, '"');
      if (q) {
        _ipAddress = String(q + 1);
        _ipAddress.replace("\"", "");
        _ipAddress.trim();
      }
    }
    if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
  }
  if (!active) TL_LOG("[GSM] Data activation failed — check APN, SIM data plan");
  return active;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT connection using native Quectel AT+QMT* commands
//
// Sequence:
//   AT+QSSLCFG=...                            — TLS settings of SSL context 0
//   AT+QMTCFG="ssl",0,1,0                     — client 0 uses TLS with context 0
//   AT+QMTCFG="will",0,1,1,1,"<topic>","OFFLINE" — last will, QoS 1, retained
//   AT+QMTOPEN=0,"<host>",<port>   → +QMTOPEN: 0,0        (0 = network open)
//   AT+QMTCONN=0,"<clientId>"[,"user","pass"] → +QMTCONN: 0,0,0  (sent, accepted)
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_quectelOpenMQTT(const String& statusTopic) {
  int r[3];

  // A session from before the ESP32 restarted may still be connected. Closing
  // it is a clean disconnect, which does not send the last will: say OFFLINE
  // first, or the retained ONLINE would stay if the new connection then fails.
  if (_quectelSessionOpen()) {
    bool wasOk = _mqttOk;
    _mqttOk = true;   // _mqttPublishAT() only publishes on a live session
    if (_mqttPublishAT(statusTopic.c_str(), "OFFLINE", true)) {
      TL_LOG("[MQTT] Status → OFFLINE (closing the previous session)");
    }
    _mqttOk = wasOk;
  }

  // Stop any previous session cleanly (errors mean there was none)
  _quectelResult("AT+QMTDISC=0", "+QMTDISC:", 10000, r, 2);
  _quectelResult("AT+QMTCLOSE=0", "+QMTCLOSE:", 10000, r, 2);

  // TLS settings for SSL context 0. Set before every connection: a modem that
  // has just started answers AT but may still refuse them.
  //   sslversion 4     → any TLS version
  //   ciphersuite      → all
  //   seclevel 0       → no certificate verification
  //   ignorelocaltime  → ignore RTC time mismatch
  //   sni 1            → send the server's name in the handshake
  const char* settings[] = {
    "AT+QSSLCFG=\"sslversion\",0,4",
    "AT+QSSLCFG=\"ciphersuite\",0,0XFFFF",
    "AT+QSSLCFG=\"seclevel\",0,0",
    "AT+QSSLCFG=\"ignorelocaltime\",0,1",
    "AT+QSSLCFG=\"sni\",0,1",
    "AT+QMTCFG=\"pdpcid\",0,1",
    "AT+QMTCFG=\"ssl\",0,1,0",
    "AT+QMTCFG=\"version\",0,4",       // MQTT 3.1.1
    "AT+QMTCFG=\"session\",0,1",       // clean session
    "AT+QMTCFG=\"recv/mode\",0,0,1",   // messages arrive in +QMTRECV with their length
  };
  for (const char* setting : settings) {
    for (int attempt = 0; attempt < 5 && !_atCmd(setting, 2000); attempt++) delay(1000);
  }
  {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+QMTCFG=\"keepalive\",0,%d", MQTT_KEEPALIVE);
    _atCmd(cmd);
  }
  {
    // Last will: OFFLINE on the status topic, QoS 1, retained
    String cmd = "AT+QMTCFG=\"will\",0,1,1,1,\"" + statusTopic + "\",\"OFFLINE\"";
    _atCmd(cmd.c_str());
  }

  // Open the network connection to the broker
  TL_LOG("[MQTT] Connecting to " MQTT_SERVER ":" + String(MQTT_PORT) + " (TLS)");
  {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"" MQTT_SERVER "\",%d", MQTT_PORT);
    r[1] = -1;
    if (!_quectelResult(cmd, "+QMTOPEN:", 30000, r, 2) || r[1] != 0) {
      TL_LOGF("[MQTT] Could not reach the broker (+QMTOPEN result %d: "
              "-1=failed, 3=no data connection, 4=name not found, 5=network down)\n", r[1]);
      return false;
    }
  }

  // MQTT login. The client ID is built as a String: it is longer than a small buffer.
  String cmd = "AT+QMTCONN=0,\"TLG_" + getChipID() + "\"";
  bool hasAuth = (_mqttUsername && _mqttUsername[0]) && (_mqttPassword && _mqttPassword[0]);
  if (hasAuth) cmd += ",\"" + String(_mqttUsername) + "\",\"" + String(_mqttPassword) + "\"";
  r[1] = -1; r[2] = -1;
  if (!_quectelResult(cmd.c_str(), "+QMTCONN:", 20000, r, 3) || r[1] != 0 || r[2] != 0) {
    TL_LOGF("[MQTT] Connect error (result %d, code %d: 4=bad login, 5=not authorised)\n",
            r[1], r[2]);
    return false;
  }
  return true;
}

// Whether MQTT client 0 in the modem is connected to the broker.
//   AT+QMTCONN?  → +QMTCONN: 0,3   (3 = connected)
bool ThingsLinkerGSM::_quectelSessionOpen() {
  _sendAT("AT+QMTCONN?");
  char line[48];
  bool open = false;
  uint32_t deadline = millis() + 2000UL;
  while (millis() < deadline && _readLine(line, sizeof(line), deadline - millis())) {
    int idx = -1, state = -1;
    if (sscanf(line, "+QMTCONN: %d,%d", &idx, &state) == 2 && idx == 0) open = (state == 3);
    if (strncmp(line, "OK", 2) == 0 || strncmp(line, "ERROR", 5) == 0) break;
  }
  return open;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT publish, QoS 1
//   AT+QMTPUBEX=0,<id>,1,<retain>,"<topic>",<len> → > → <payload> → OK
//   → +QMTPUBEX: 0,<id>,0      (0 = sent, 1 = being re-sent, 2 = failed)
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_quectelPublish(const char* topic, const char* payload, bool retain) {
  uint16_t id  = _nextMsgId();
  int      len = strlen(payload);
  String cmd = "AT+QMTPUBEX=0," + String(id) + ",1," + (retain ? "1" : "0")
               + ",\"" + topic + "\"," + String(len);
  if (!_atDataCmd(cmd.c_str(), payload, len, 5000)) {
    TL_LOG("[MQTT] QMTPUBEX failed");
    return false;
  }

  char line[64];
  uint32_t deadline = millis() + 15000UL;
  while (millis() < deadline) {
    if (!_readLine(line, sizeof(line), deadline - millis())) break;
    int idx = -1, msgId = -1, result = -1;
    if (sscanf(line, "+QMTPUBEX: %d,%d,%d", &idx, &msgId, &result) == 3 && msgId == id) {
      if (result == 0) return true;
      if (result == 1) continue;   // the modem is re-sending it
      TL_LOGF("[MQTT] Publish error %d\n", result);
      return false;
    }
    if (strncmp(line, "ERROR", 5) == 0) return false;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT subscribe, QoS 1
//   AT+QMTSUB=0,<id>,"<topic>",1  → OK → +QMTSUB: 0,<id>,0,<granted qos>
// ─────────────────────────────────────────────────────────────────────────────
bool ThingsLinkerGSM::_quectelSubscribe(const char* topic) {
  uint16_t id = _nextMsgId();
  String cmd = "AT+QMTSUB=0," + String(id) + ",\"" + topic + "\",1";
  _sendAT(cmd.c_str());

  char line[64];
  uint32_t deadline = millis() + 10000UL;
  while (millis() < deadline) {
    if (!_readLine(line, sizeof(line), deadline - millis())) break;
    int idx = -1, msgId = -1, result = -1;
    if (sscanf(line, "+QMTSUB: %d,%d,%d", &idx, &msgId, &result) == 3 && msgId == id) {
      bool ok = (result == 0);
      TL_LOGF("[MQTT] %s sub: %s\n", topic, ok ? "OK" : "FAIL");
      return ok;
    }
    if (strncmp(line, "ERROR", 5) == 0) return false;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Incoming message and lost-connection lines
//
//   +QMTRECV: <client>,<msgid>,"<topic>",<length>,"<payload>"
//   +QMTRECV: <client>,<msgid>,"<topic>","<payload>"      (without the length)
//   +QMTSTAT: <client>,<error>                           (connection closed)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_quectelProcessLine(const char* line) {
  if (strncmp(line, "+QMTRECV:", 9) == 0) {
    // Skip the client index and message id
    const char* p = strchr(line + 9, ',');
    if (p) p = strchr(p + 1, ',');
    if (!p || p[1] != '"') return;
    p += 2;

    const char* end = strchr(p, '"');
    if (!end) return;
    size_t topicLen = (size_t)(end - p);
    if (topicLen == 0 || topicLen >= sizeof(_rxTopic)) return;
    memcpy(_rxTopic, p, topicLen);
    _rxTopic[topicLen] = '\0';

    p = end + 1;
    if (*p != ',') return;
    p++;

    // The payload's own length, when the modem sends it: exact even when the
    // payload holds quotes. Otherwise the payload runs to the last quote.
    int length = -1;
    if (*p >= '0' && *p <= '9') {
      length = atoi(p);
      while (*p >= '0' && *p <= '9') p++;
      if (*p == ',') p++;
    }
    if (*p == '"') p++;
    const char* last = strrchr(p, '"');
    size_t available = last ? (size_t)(last - p) : strlen(p);
    size_t payloadLen = (length >= 0 && (size_t)length < available) ? (size_t)length : available;
    if (payloadLen == 0 || payloadLen >= sizeof(_rxPayload)) return;
    memcpy(_rxPayload, p, payloadLen);
    _rxPayload[payloadLen] = '\0';

    _queueMessage(_rxTopic, _rxPayload);
    _rxTopic[0]   = '\0';
    _rxPayload[0] = '\0';
    return;
  }

  if (strncmp(line, "+QMTSTAT:", 9) == 0) {
    int idx = 0, err = 0;
    sscanf(line + 9, " %d,%d", &idx, &err);
    TL_LOGF("[MQTT] Connection lost (cause=%d) — will reconnect\n", err);
    _mqttOk = false;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Time from an internet time server; the modem sets its own clock from it.
//   AT+QNTP=1,"<server>",123,1  → OK → +QNTP: 0,"2026/11/08,10:15:30+22"
// Returns 0 when the clock was updated.
// ─────────────────────────────────────────────────────────────────────────────
int ThingsLinkerGSM::_quectelSyncTime() {
  int r[1] = { -1 };
  if (!_quectelResult("AT+QNTP=1,\"" NTP_SERVER_1 "\",123,1", "+QNTP:", 20000, r, 1)) return -1;
  return r[0];
}

// ─────────────────────────────────────────────────────────────────────────────
// GPS — only EC200A variants with GNSS have it
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinkerGSM::_quectelStartGPS() {
  // Fails with +CME ERROR: 504 when it is already running, which is fine
  _atCmd("AT+QGPS=1", 3000);
}

// Parse a position read with AT+QGPSLOC=2 (degrees as decimals).
// Format: +QGPSLOC: <utc>,<lat>,<lon>,<hdop>,<alt>,<fix>,<cog>,<km/h>,<knots>,<date>,<sats>
// No fix: +CME ERROR: 516
bool ThingsLinkerGSM::_quectelParseGps(const char* line) {
  const char* p = strstr(line, "+QGPSLOC:");
  if (!p) return false;
  p += 9;
  char utc[16] = {};
  float lat = 0, lng = 0, hdop = 0, alt = 0, cog = 0, kmh = 0;
  int fix = 0;
  int n = sscanf(p, " %15[^,],%f,%f,%f,%f,%d,%f,%f", utc, &lat, &lng, &hdop, &alt, &fix, &cog, &kmh);
  if (n < 3) { _gpsValid = false; return false; }

  _gpsLat      = lat;
  _gpsLng      = lng;
  _gpsAltM     = n >= 5 ? alt : 0;
  _gpsSpeedKmh = n >= 8 ? kmh : 0;
  _gpsValid    = true;
  return true;
}
