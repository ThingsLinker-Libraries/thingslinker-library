/**
 * @file TL_OTA.cpp
 * @brief OTA firmware update implementation for ESP32
 */

#include "TL_OTA.h"
#include "TL_MQTT.h"
#include "TL_Config.h"

#ifdef ESP32

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>   // esp_ota_get_next_update_partition()

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper — report OTA status back to the backend
// ─────────────────────────────────────────────────────────────────────────────

// ── Clients for the REST calls (check, status) ────────────────────────────────
// The server address decides: https:// (the live API) needs TLS, while
// http:// (a server on the LAN) is plain TCP. Sending plain HTTP to an https
// server's port gets a 404 from its proxy, so the check would always fail.
static WiFiClient       _otaHttpClient;
static WiFiClientSecure _otaHttpsClient;
static HTTPUpdate       _otaUpdater(30000);   // firmware download, 30 s data timeout

static bool _otaBegin(HTTPClient& http, const String& url)
{
  if (url.startsWith("https")) {
    _otaHttpsClient.setInsecure();   // same as the firmware download below
    return http.begin(_otaHttpsClient, url);
  }
  return http.begin(_otaHttpClient, url);
}

static bool _otaReportStatus(const char* authToken,
                              const char* apiServer,
                              const String& otaDeviceId,
                              const char* statusStr,
                              int progressPct   = -1,
                              const char* errMsg = nullptr)
{
  HTTPClient http;
  String url = String(apiServer) + "/api/v1/org/ota/device/update-status/";

  // Explicit client — the deprecated single-arg http.begin() can fail silently
  // when a WiFiClientSecure (MQTT TLS) is also active.
  if (!_otaBegin(http, url)) {
    TL_LOG("[OTA] reportStatus: http.begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", authToken);

  // Build JSON body
  JsonDocument doc;
  doc["ota_device_id"] = otaDeviceId;
  doc["status"]        = statusStr;
  if (progressPct >= 0)  doc["progress_percentage"] = progressPct;
  if (errMsg)            doc["error_message"]        = errMsg;

  String body;
  serializeJson(doc, body);

  int code = http.PUT(body);
  http.end();

  if (code != 200) {
    TL_LOGF("[OTA] reportStatus HTTP %d\n", code);
    return false;
  }
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

OTAResult checkAndApplyOTA(const char* authToken, const char* apiServer)
{
  // ── Step 1: Check for a pending update ──────────────────────────────────────
  String checkUrl = String(apiServer) + "/api/v1/org/ota/device/check/";
  TL_LOGF("[OTA] Checking: %s\n", checkUrl.c_str());

  HTTPClient http;
  // Explicit client — the single-arg http.begin(url) form is deprecated
  // on ESP32 Arduino and returns HTTP -1 when a WiFiClientSecure (MQTT) is active.
  if (!_otaBegin(http, checkUrl)) {
    TL_LOG("[OTA] http.begin failed");
    return OTA_ERROR;
  }

  http.addHeader("X-Device-Token", authToken);
  http.setTimeout(10000);  // 10 s timeout — prevents indefinite hang on slow networks
  int code = http.GET();

  if (code != 200) {
    TL_LOGF("[OTA] Check failed: HTTP %d\n", code);
    http.end();
    return OTA_ERROR;
  }

  String payload = http.getString();
  http.end();

  // ── Parse JSON response ──────────────────────────────────────────────────────
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    TL_LOGF("[OTA] JSON parse error: %s\n", err.c_str());
    return OTA_ERROR;
  }

  bool hasUpdate = doc["has_update"] | false;
  if (!hasUpdate) {
    TL_LOG("[OTA] No update available.");
    return OTA_NO_UPDATE;
  }

  String otaDeviceId   = doc["ota_device_id"].as<String>();
  String firmwareUrl   = doc["firmware_url"].as<String>();
  String version       = doc["firmware_version"] | "?";
  int    firmwareBytes = doc["firmware_file_size"] | 0;

  TL_LOGF("[OTA] Pending update: v%s\n", version.c_str());
  TL_LOGF("[OTA] Firmware URL: %s\n", firmwareUrl.c_str());

  // ── Pre-flash safety checks (run BEFORE writing a single byte) ───────────────

  // Check 1 — OTA partition exists
  // esp_ota_get_next_update_partition() returns NULL when the device was flashed
  // with a partition scheme that has no OTA slots (e.g. "No OTA", "Huge APP",
  // "Minimal SPIFFS"). This means the base firmware and new firmware were compiled
  // with different partition schemes — flashing would brick the device.
  const esp_partition_t* otaPart = esp_ota_get_next_update_partition(NULL);
  if (!otaPart) {
    TL_LOG("[OTA] ABORT: No OTA partition found on this device.");
    TL_LOG("[OTA] Re-flash via USB with the correct partition scheme:");
    TL_LOG("[OTA]   Arduino IDE → Tools → Partition Scheme");
    TL_LOG("[OTA]   → Default with OTA (1.3MB APP / 1.5MB SPIFFS)");
    TL_LOG("[OTA] Both base firmware AND new .bin must use the same scheme.");
    _otaReportStatus(authToken, apiServer, otaDeviceId, "failed", -1,
                     "No OTA partition — device was flashed without OTA partition scheme");
    return OTA_FAILED;
  }
  TL_LOGF("[OTA] OTA partition OK — slot: %s  size: %lu bytes\n",
          otaPart->label, (unsigned long)otaPart->size);

  // Check 2 — Firmware fits inside the OTA partition
  // If the new binary was compiled with a larger app size (different partition
  // scheme) it will exceed the available slot and the flash will fail mid-write.
  if (firmwareBytes > 0 && (uint32_t)firmwareBytes > otaPart->size) {
    TL_LOGF("[OTA] ABORT: Firmware size %d bytes exceeds OTA slot size %lu bytes.\n",
            firmwareBytes, (unsigned long)otaPart->size);
    TL_LOG("[OTA] The new .bin may have been compiled with a different partition scheme.");
    TL_LOG("[OTA] Compile both sketches with the same scheme and re-upload the .bin.");
    _otaReportStatus(authToken, apiServer, otaDeviceId, "failed", -1,
                     "Firmware too large for OTA partition — partition scheme mismatch");
    return OTA_FAILED;
  }

  // ── Step 2: Report in_progress ───────────────────────────────────────────────
  _otaReportStatus(authToken, apiServer, otaDeviceId, "in_progress", 0);

  // Close MQTT first: while the download blocks the loop, nothing reads the MQTT
  // socket, and its incoming data can use up the network buffers the download
  // needs (seen as the download stalling part-way). run() reconnects if the
  // update fails; on success the device restarts anyway.
  if (isMQTTConnected()) disconnectMQTT();

  // WiFi modem sleep (on by default) can pause the radio long enough to stall a
  // long TLS download; keep the radio awake until the update ends.
  bool wifiSlept = WiFi.getSleep();
  WiFi.setSleep(false);

  // ── Step 3: Download + flash the firmware ────────────────────────────────────
  // httpUpdate handles download, CRC check, and flash write internally.
  // The device reboots into the new firmware on HTTP_UPDATE_OK.

#ifdef LED_BUILTIN
  _otaUpdater.setLedPin(LED_BUILTIN, LOW);  // Blink LED during flash
#endif

  // Restart ourselves after reporting "completed" (step 4); by default the
  // updater restarts the moment flashing ends and the report is never sent.
  _otaUpdater.rebootOnUpdate(false);

  // Progress on Serial every 10 %, so a stalled download shows where it stopped
  _otaUpdater.onProgress([](int done, int total) {
    static int nextPct = 0;
    int pct = total > 0 ? (int)((int64_t)done * 100 / total) : 0;
    if (done == 0) nextPct = 0;
    if (pct >= nextPct) {
      TL_LOGF("[OTA] Downloaded %d%% (%d / %d bytes)\n", pct, done, total);
      nextPct = pct + 10;
    }
  });

  t_httpUpdate_return result;
  bool useTLS = firmwareUrl.startsWith("https");

  // The updater gives up when no data arrives within its HTTP timeout (8 s by
  // default). A TLS download beside the MQTT TLS connection can pause longer,
  // so _otaUpdater allows 30 s.
  if (useTLS) {
    WiFiClientSecure client;
    client.setInsecure();   // Skip cert verification — acceptable for IoT OTA
    result = _otaUpdater.update(client, firmwareUrl);
  } else {
    WiFiClient client;
    result = _otaUpdater.update(client, firmwareUrl);
  }

  // ── Step 4: Report result ────────────────────────────────────────────────────
  if (result == HTTP_UPDATE_OK) {
    // Report success before restarting (best-effort — device may restart first)
    _otaReportStatus(authToken, apiServer, otaDeviceId, "completed", 100);
    TL_LOG("[OTA] Flash successful — restarting in 1 s...");
    delay(1000);
    ESP.restart();
    return OTA_SUCCESS;  // Never reached
  }

  // Flash failed
  WiFi.setSleep(wifiSlept);
  String errDetail = "Code " + String(_otaUpdater.getLastError())
                     + ": " + _otaUpdater.getLastErrorString();
  TL_LOGF("[OTA] Failed — %s\n", errDetail.c_str());
  _otaReportStatus(authToken, apiServer, otaDeviceId, "failed", -1, errDetail.c_str());
  return OTA_FAILED;
}

#else  // ── Non-ESP32 stub ────────────────────────────────────────────────────

OTAResult checkAndApplyOTA(const char* /*authToken*/, const char* /*apiServer*/)
{
  TL_LOG("[OTA] OTA is only supported on ESP32.");
  return OTA_ERROR;
}

#endif // ESP32
