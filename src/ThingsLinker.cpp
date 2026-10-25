/**
 * @file ThingsLinker.cpp
 * @brief ThingsLinker library — main class implementation
 */

#include "ThingsLinker.h"
#include <WiFi.h>

// ── Global debug flag (controlled via iot.debug()) ───────────────────────────
// Declared extern in TL_Config.h so all modules can read it via TL_LOG().
bool _tlDebugEnabled = TL_DEBUG_DEFAULT;

// Set by the MQTT command callback, acted on in run(): the reset restarts the
// device, which must not happen in the middle of the MQTT client's own loop.
static volatile bool _remoteResetRequested = false;

static void onDeviceCommand(const char* cmd) {
  if (strcmp(cmd, "reset_wifi") == 0) _remoteResetRequested = true;
  else TL_LOG("[MQTT] Unknown command ignored: " + String(cmd));
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ThingsLinker::ThingsLinker(const char* authToken, const char* blueprintId)
  : _authToken(authToken),
    _blueprintId(blueprintId),
    _mqttUsername(nullptr),
    _mqttPassword(nullptr),
    _bleBrandName("ThingsLinker"),
    _initialized(false),
    _lastCheck(0),
    _resetPin(-1),
    _resetHoldMs(TL_RESET_HOLD_MS),
    _resetActiveLow(true),
    _resetPressedAt(0),
    _remoteReset(true) {
}

// ─────────────────────────────────────────────────────────────────────────────
// begin()
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::begin(const char* mqttUsername, const char* mqttPassword) {
  _mqttUsername = mqttUsername;
  _mqttPassword = mqttPassword;

  // Initialise Serial if the sketch hasn't already done so.
  if (!Serial) {
    Serial.begin(115200);
    delay(100);
  }

  TL_LOG("\n========================================");
  TL_LOG("   ThingsLinker IoT Library v2.0");
  TL_LOG("========================================");
  TL_LOG("Chip ID : " + getChipID());
  TL_LOG("Broker  : " MQTT_SERVER ":" + String(MQTT_PORT));
  TL_LOG("========================================\n");

  onMQTTCommand(_remoteReset ? onDeviceCommand : nullptr);

  if (hasWiFiCredentials()) {
    if (!isSavedNetworkVisible()) {
      // SSID not visible — skip the full timeout and go straight to BLE.
      TL_LOG("[Setup] Saved network not in range — starting BLE...");
      handleBLEProvisioning();
    } else {
      TL_LOG("[Setup] Saved WiFi found, connecting...");
      if (connectWiFi()) {
        TL_LOG("[Setup] ✓ WiFi connected");
        if (connectMQTT(_authToken, _blueprintId, _mqttUsername, _mqttPassword)) {
          TL_LOG("[Setup] ✓ MQTT connected — device ready!");
        }
      } else {
        // SSID visible but connection failed (wrong password / auth issue).
        // Clear credentials so next boot skips the timeout.
        TL_LOG("[Setup] WiFi failed — clearing credentials, starting BLE...");
        clearWiFiCredentials();
        handleBLEProvisioning();
      }
    }
  } else {
    TL_LOG("[Setup] No WiFi saved — starting BLE...");
    handleBLEProvisioning();
  }

  _initialized = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// run()
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::run() {
  if (!_initialized) return;

  // BLE provisioning is blocking from the MQTT perspective — skip everything
  // else while the app is setting up the device.
  if (isBLEActive()) return;

  checkResetButton();

  loopMQTT();

  if (_remoteResetRequested) {
    _remoteResetRequested = false;
    TL_LOG("[Reset] Reset WiFi requested from the app or portal");
    resetWiFi();
    return;
  }

  // Lightweight connection health check every 5 s.
  unsigned long now = millis();
  if (now - _lastCheck >= 5000UL) {
    _lastCheck = now;
    checkConnections();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE provisioning
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::handleBLEProvisioning() {
  String deviceName = String(_bleBrandName) + "_" + getChipID();

  onBLEDisconnected([]() {
    TL_LOG("[BLE] App disconnected");
  });

  // When the app sends WiFi credentials, attempt connection and report status.
  onBLECredentialsReceived([](String ssid, String password) {
    TL_LOG("[BLE] Attempting WiFi: " + ssid);

    bool ok = connectWiFi(ssid.c_str(), password.c_str(), true);
    sendBLEStatus(ok, ok ? getWiFiIP() : "");

    if (ok) {
      TL_LOG("[BLE] ✓ WiFi OK — waiting for app to confirm onboarding...");
    } else {
      TL_LOG("[BLE] ✗ WiFi failed — BLE still active for retry");
    }
  });

  startBLE(deviceName.c_str());

  TL_LOG("\n========================================");
  TL_LOG("  BLE PROVISIONING ACTIVE");
  TL_LOG("========================================");
  TL_LOG("1. Open ThingsLinker app");
  TL_LOG("2. Tap  \"Add Device\"  →  scan for BLE");
  TL_LOG("3. Select: " + deviceName);
  TL_LOG("4. Enter WiFi credentials");
  TL_LOG("========================================\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection health check (called from run() every 5 s)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::checkConnections() {
  if (!isWiFiConnected() && hasWiFiCredentials()) {
    TL_LOG("[Check] WiFi lost — reconnecting...");
    connectWiFi();
  }

  if (isWiFiConnected() && !isMQTTConnected()) {
    TL_LOG("[Check] MQTT lost — reconnecting...");
    connectMQTT(_authToken, _blueprintId, _mqttUsername, _mqttPassword);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget publish
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::button (const char* pin, bool  v) { publishMQTT("Button",        pin, v); }
void ThingsLinker::led    (const char* pin, bool  v) { publishMQTT("LED",            pin, v); }
void ThingsLinker::map    (const char* pin, float lat, float lng) { publishMQTTMap(pin, lat, lng); }
void ThingsLinker::gauge  (const char* pin, float v) { publishMQTT("Gauge",          pin, v); }
void ThingsLinker::chart  (const char* pin, float v) { publishMQTT("Chart",          pin, v); }
void ThingsLinker::slider (const char* pin, float v) { publishMQTT("Slider",         pin, v); }
void ThingsLinker::display (const char* pin, const TLValue& v) { publishMQTT("Value Display", pin, v); }
void ThingsLinker::label   (const char* pin, const TLValue& v) { publishMQTT("Label",         pin, v); }
void ThingsLinker::send    (const char* pin, const TLValue& v) { publishMQTT("Value Display", pin, v); }
// Published on the display channel so the device's own Terminal subscription
// (onTerminal) does not receive it back. Apps match Terminal output by pin.
void ThingsLinker::terminal(const char* pin, const TLValue& v) { publishMQTT("Terminal",      pin, v); }
void ThingsLinker::status     (const char* pin, const TLValue& v) { publishMQTT("Status",       pin, v); }
void ThingsLinker::progress   (const char* pin, float v)          { publishMQTT("Progress Bar", pin, v); }
void ThingsLinker::selector   (const char* pin, const TLValue& v) { publishMQTT("Selector",     pin, v); }
void ThingsLinker::numberInput(const char* pin, float v)          { publishMQTT("Number Input", pin, v); }
void ThingsLinker::publish(const char* widgetType, const char* pin, const TLValue& v) { publishMQTT(widgetType, pin, v); }

// ─────────────────────────────────────────────────────────────────────────────
// Widget subscribe
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::onButton(const char* pin, void (*cb)(bool))  { subscribeMQTTButton("Button", pin, cb); }
void ThingsLinker::onLED   (const char* pin, void (*cb)(bool))  { subscribeMQTTButton("LED",    pin, cb); }
void ThingsLinker::onSwitch(const char* pin, void (*cb)(bool))  { subscribeMQTTButton("Switch", pin, cb); }
void ThingsLinker::onSlider  (const char* pin, void (*cb)(float))                                   { subscribeMQTT("Slider",        pin, cb); }
void ThingsLinker::onValue   (const char* pin, void (*cb)(float))                                   { subscribeMQTT("Value Display", pin, cb); }
void ThingsLinker::onTerminal(const char* pin, void (*cb)(const char*))                             { subscribeMQTTText("Terminal",  pin, cb); }
void ThingsLinker::onWidget    (const char* widgetType, const char* pin, void (*cb)(float))       { subscribeMQTT(widgetType,     pin, cb); }
void ThingsLinker::onWidgetText(const char* widgetType, const char* pin, void (*cb)(const char*)) { subscribeMQTTText(widgetType, pin, cb); }
void ThingsLinker::onSelector    (const char* pin, void (*cb)(float))       { subscribeMQTT("Selector",         pin, cb); }
void ThingsLinker::onSelectorText(const char* pin, void (*cb)(const char*)) { subscribeMQTTText("Selector",     pin, cb); }
void ThingsLinker::onNumberInput (const char* pin, void (*cb)(float))       { subscribeMQTT("Number Input",     pin, cb); }
void ThingsLinker::onTextInput   (const char* pin, void (*cb)(const char*)) { subscribeMQTTText("Text Input",   pin, cb); }
void ThingsLinker::onKeypad      (const char* pin, void (*cb)(float))       { subscribeMQTT("Keypad",           pin, cb); }
void ThingsLinker::onKeypadText  (const char* pin, void (*cb)(const char*)) { subscribeMQTTText("Keypad",       pin, cb); }
void ThingsLinker::onRGB     (const char* pin, void (*cb)(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern)) { subscribeRGBMQTT(pin, cb); }
void ThingsLinker::onTimer   (const char* pin, void (*cb)(float))                                   { subscribeMQTT("Timer",         pin, cb); }
void ThingsLinker::onJoystick(const char* pin, void (*cb)(float x, float y))                        { subscribeJoystickMQTT(pin, cb);           }

// ─────────────────────────────────────────────────────────────────────────────
// Status
// ─────────────────────────────────────────────────────────────────────────────
bool   ThingsLinker::wifiConnected() { return isWiFiConnected(); }
bool   ThingsLinker::mqttConnected() { return isMQTTConnected(); }
bool   ThingsLinker::bleActive()     { return isBLEActive();    }
String ThingsLinker::getIP()         { return getWiFiIP();      }
String ThingsLinker::getChipID()     { return ::getChipID();    }

// ─────────────────────────────────────────────────────────────────────────────
// Storage (delegate to TL_Storage)
// ─────────────────────────────────────────────────────────────────────────────
bool   ThingsLinker::saveString(const char* k, const String& v) { return ::saveString(k, v); }
String ThingsLinker::getString (const char* k, const String& d) { return ::getString(k, d);  }
bool   ThingsLinker::saveInt   (const char* k, int v)           { return ::saveInt(k, v);   }
int    ThingsLinker::getInt    (const char* k, int d)           { return ::getInt(k, d);    }
bool   ThingsLinker::saveFloat (const char* k, float v)         { return ::saveFloat(k, v); }
float  ThingsLinker::getFloat  (const char* k, float d)         { return ::getFloat(k, d);  }
bool   ThingsLinker::saveBool  (const char* k, bool v)          { return ::saveBool(k, v);  }
bool   ThingsLinker::getBool   (const char* k, bool d)          { return ::getBool(k, d);   }
bool   ThingsLinker::hasKey    (const char* k)                  { return ::hasKey(k);       }
bool   ThingsLinker::removeKey (const char* k)                  { return ::removeKey(k);    }
void   ThingsLinker::clearAllData()                             { ::clearAllData();          }

// ─────────────────────────────────────────────────────────────────────────────
// Offline storage (delegate to TL_Offline)
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::offlineStorage(bool enable, uint16_t maxRecords) {
  tlOfflineEnable(enable, maxRecords);
  TL_LOG(enable ? "[Offline] Storage on — up to " + String(maxRecords) + " readings"
                : String("[Offline] Storage off"));
}
uint16_t ThingsLinker::offlineCount() { return tlOfflineCount(); }

// ─────────────────────────────────────────────────────────────────────────────
// OTA
// ─────────────────────────────────────────────────────────────────────────────
OTAResult ThingsLinker::checkOTA(const char* apiServer) {
  return checkAndApplyOTA(_authToken, apiServer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Advanced
// ─────────────────────────────────────────────────────────────────────────────
void ThingsLinker::setBLEName(const char* brandName) {
  _bleBrandName = brandName;
}

void ThingsLinker::resetWiFi() {
  TL_LOG("[Reset] Clearing WiFi credentials...");
  clearWiFiCredentials();
  disconnectMQTT();

  // If WiFi (and the secure MQTT connection) ran in this boot, start setup
  // mode from a clean restart. Starting Bluetooth on top of a WiFi stack that
  // was in use left too little for the new WiFi connection, so the device
  // received the new network but never joined it. With no network saved, the
  // restart goes straight into setup mode, the same path as a first boot.
  if (WiFi.getMode() != WIFI_OFF) {
    TL_LOG("[Reset] Restarting into setup mode...");
    delay(300);   // let the Serial line and the MQTT close go out
    ESP.restart();
  }

  disconnectWiFi();
  TL_LOG("[Reset] Starting BLE provisioning...");
  handleBLEProvisioning();
}

void ThingsLinker::resetButton(uint8_t pin, uint16_t holdMs, bool activeLow) {
  _resetPin       = pin;
  _resetHoldMs    = holdMs;
  _resetActiveLow = activeLow;
  _resetPressedAt = 0;
  pinMode(pin, activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
  TL_LOG("[Reset] Hold the button on GPIO " + String(pin) + " for " +
         String(holdMs / 1000.0f, 1) + " s to reset WiFi");
}

void ThingsLinker::remoteReset(bool enable) {
  _remoteReset = enable;
  if (_initialized) onMQTTCommand(enable ? onDeviceCommand : nullptr);
}

// Called from run(): a long press of the reset button resets WiFi.
void ThingsLinker::checkResetButton() {
  if (_resetPin < 0) return;
  bool pressed = digitalRead(_resetPin) == (_resetActiveLow ? LOW : HIGH);
  if (!pressed) {
    _resetPressedAt = 0;
    return;
  }
  unsigned long now = millis();
  if (_resetPressedAt == 0) {
    _resetPressedAt = now ? now : 1;
    TL_LOG("[Reset] Button pressed — keep holding to reset WiFi...");
    return;
  }
  if (now - _resetPressedAt >= _resetHoldMs) {
    _resetPressedAt = 0;
    TL_LOG("[Reset] Button held — resetting WiFi");
    resetWiFi();
  }
}

void ThingsLinker::debug(bool enable) {
  _tlDebugEnabled = enable;
}
