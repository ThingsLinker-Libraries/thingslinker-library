/**
 * @file TL_BLE.cpp
 * @brief BLE provisioning — lets the ThingsLinker mobile app send WiFi credentials
 *        to an ESP32 over Bluetooth Low Energy.
 *
 * Flow:
 *  1. startBLE() advertises the device.
 *  2. App connects, writes {"ssid":"…","password":"…"} to the WiFi characteristic.
 *  3. Firmware calls connectWiFi(); on success sends {"status":"connected","ip":"…"}.
 *  4. App confirms onboarding by writing {"status":"complete"} to the confirm characteristic.
 *  5. Firmware restarts — next boot connects directly to WiFi and MQTT.
 */

#include "TL_BLE.h"
#include "TL_Config.h"
#include <ArduinoJson.h>

#ifdef ESP32

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
static BLEServer*         _bleServer         = nullptr;
static BLECharacteristic* _wifiChar          = nullptr;
static BLECharacteristic* _statusChar        = nullptr;
static BLECharacteristic* _confirmChar       = nullptr;
static bool               _bleActive         = false;
static void (*_credCb)(String, String)        = nullptr;
static void (*_disconnectCb)()               = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// BLE server callbacks
// ─────────────────────────────────────────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    TL_LOG("[BLE] App connected");
  }
  void onDisconnect(BLEServer* server) override {
    TL_LOG("[BLE] App disconnected");
    if (_disconnectCb) _disconnectCb();
    // An ESP32 stops advertising once something connects. Advertise again so
    // the app or browser can reconnect, for example after the link dropped
    // while the user was typing the WiFi details.
    if (_bleActive && server) {
      server->startAdvertising();
      TL_LOG("[BLE] Advertising again");
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// WiFi credential characteristic — app writes {"ssid":"…","password":"…"}
// ─────────────────────────────────────────────────────────────────────────────
class WiFiCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    String raw = ch->getValue().c_str();
    if (raw.length() == 0) return;

    TL_LOG("[BLE] Received: " + raw);

    JsonDocument doc;
    if (deserializeJson(doc, raw.c_str()) != DeserializationError::Ok) {
      TL_LOG("[BLE] JSON parse error");
      return;
    }

    const char* ssid     = doc["ssid"];
    const char* password = doc["password"];

    if (!ssid || !password) {
      TL_LOG("[BLE] Missing ssid or password field");
      return;
    }

    TL_LOG("[BLE] WiFi credentials received for SSID: " + String(ssid));
    if (_credCb) _credCb(String(ssid), String(password));
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Confirm characteristic — app writes {"status":"complete"} after backend
// registration succeeds, triggering a restart into normal MQTT mode.
// ─────────────────────────────────────────────────────────────────────────────
class ConfirmCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    String raw = ch->getValue().c_str();
    if (raw.length() == 0) return;

    TL_LOG("[BLE] Confirmation: " + raw);

    JsonDocument doc;
    if (deserializeJson(doc, raw.c_str()) != DeserializationError::Ok) return;

    const char* status = doc["status"];
    if (status && strcmp(status, "complete") == 0) {
      TL_LOG("[BLE] ✓ Onboarding confirmed — restarting...");
      Serial.flush();
      delay(300);
      ESP.restart();
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool startBLE(const char* deviceName) {
  if (_bleActive) {
    TL_LOG("[BLE] Already active");
    return false;
  }

  TL_LOG("[BLE] Starting... Device: " + String(deviceName));

  BLEDevice::init(deviceName);

  _bleServer = BLEDevice::createServer();
  _bleServer->setCallbacks(new ServerCallbacks());

  BLEService* svc = _bleServer->createService(BLE_SERVICE_UUID);

  // WiFi credential characteristic (write only)
  _wifiChar = svc->createCharacteristic(
    BLE_WIFI_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  _wifiChar->setCallbacks(new WiFiCharCallbacks());

  // Status characteristic (read + notify — firmware sends connection result)
  _statusChar = svc->createCharacteristic(
    BLE_STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  _statusChar->addDescriptor(new BLE2902());

  // Confirm characteristic (write — app signals successful backend registration)
  _confirmChar = svc->createCharacteristic(
    BLE_CONFIRM_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  _confirmChar->setCallbacks(new ConfirmCharCallbacks());

  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  _bleActive = true;
  TL_LOG("[BLE] ✓ Advertising as: " + String(deviceName));
  return true;
}

void stopBLE() {
  if (!_bleActive) return;

  if (_bleServer) {
    _bleServer->getAdvertising()->stop();
    BLEDevice::deinit(true);
    _bleServer  = nullptr;
    _wifiChar   = nullptr;
    _statusChar = nullptr;
    _confirmChar = nullptr;
  }

  _bleActive = false;
  TL_LOG("[BLE] Stopped");
}

bool isBLEActive() {
  return _bleActive;
}

void onBLECredentialsReceived(void (*callback)(String ssid, String password)) {
  _credCb = callback;
}

void sendBLEStatus(bool connected, const String& ip) {
  if (!_bleActive || !_statusChar) return;

  JsonDocument doc;
  doc["status"] = connected ? "connected" : "failed";
  doc["ip"]     = ip;

  String json;
  serializeJson(doc, json);

  _statusChar->setValue(json.c_str());
  _statusChar->notify();

  TL_LOG("[BLE] Status sent: " + json);
}

void onBLEDisconnected(void (*callback)()) {
  _disconnectCb = callback;
}

void onBLEOnboardingComplete(void (*callback)()) {
  // Retained for API compatibility. The confirm characteristic fires ESP.restart()
  // directly, so a separate callback is not needed.
  (void)callback;
}

#else
// ─────────────────────────────────────────────────────────────────────────────
// Stub implementations for non-ESP32 platforms
// ─────────────────────────────────────────────────────────────────────────────
bool startBLE(const char*) { return false; }
void stopBLE()              {}
bool isBLEActive()          { return false; }
void onBLECredentialsReceived(void (*)(String, String)) {}
void sendBLEStatus(bool, const String&) {}
void onBLEDisconnected(void (*)()) {}
void onBLEOnboardingComplete(void (*)()) {}

#endif // ESP32
