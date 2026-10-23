# ThingsLinker Library Architecture - Complete Step-by-Step Guide

**Date**: 2025-11-12
**Version**: 2.0
**Platform**: ESP32 (Arduino Framework)

---

## 📚 Table of Contents

1. [Library Structure](#library-structure)
2. [Component Overview](#component-overview)
3. [Step-by-Step Flow](#step-by-step-flow)
4. [Configuration System](#configuration-system)
5. [Storage System](#storage-system)
6. [BLE Provisioning Flow](#ble-provisioning-flow)
7. [WiFi Connection Flow](#wifi-connection-flow)
8. [MQTT Connection & Topics](#mqtt-connection--topics)
9. [Widget System](#widget-system)
10. [Main Wrapper Class](#main-wrapper-class)
11. [Complete Execution Flow](#complete-execution-flow)
12. [Data Flow Diagrams](#data-flow-diagrams)

---

## Library Structure

```
ThingsLinker/src/
├── ThingsLinker.h         # Main wrapper class (public API)
├── ThingsLinker.cpp       # Main implementation
├── TL_Config.h            # Configuration constants
├── TL_Storage.h/.cpp      # ESP32 Preferences (persistent storage)
├── TL_BLE.h/.cpp          # BLE provisioning system
├── TL_WiFi.h/.cpp         # WiFi management
├── TL_MQTT.h/.cpp         # MQTT client & topic management
└── TL_Base64.h/.cpp       # Base64 encoding/decoding utilities
```

---

## Component Overview

### 1. **TL_Config.h** - Configuration File

**Purpose**: Centralized configuration for all library settings

**Key Settings**:
```cpp
#define MAX_VIRTUAL_PINS 125              // V0-V124 support
#define MQTT_SERVER "mqtt.thingslinker.com"
#define MQTT_PORT 8883  // TLS/SSL
#define BLE_DEVICE_NAME_PREFIX "ThingsLinker_"
#define WIFI_CONNECT_TIMEOUT 30000        // 30 seconds
#define MQTT_KEEPALIVE 60
```

**Supports**:
- Production MQTT server (mqtt.thingslinker.com)
- Test MQTT server (mqtt.thingslinker.com) - commented by default
- BLE service and characteristic UUIDs
- Connection timeouts and intervals

---

### 2. **TL_Storage** - ESP32 Preferences Storage

**Purpose**: Persistent flash storage for app data (separate from WiFi credentials)

**Namespace**: `"tl_app"`

**Key Functions**:
```cpp
bool saveString(const char* key, const String& value);
String getString(const char* key, const String& defaultValue);
bool saveInt(const char* key, int value);
int getInt(const char* key, int defaultValue);
bool saveFloat(const char* key, float value);
float getFloat(const char* key, float defaultValue);
bool saveBool(const char* key, bool value);
bool getBool(const char* key, bool defaultValue);
bool hasKey(const char* key);
bool removeKey(const char* key);
void clearAllData();
```

**Implementation Details**:
- Uses ESP32 `Preferences` library
- Opens namespace in **read/write mode** (`false` parameter)
- Lazy initialization (opens on first use)
- Separate from WiFi credentials namespace

**Use Case**:
```cpp
// Mobile app sends device name
iot.saveString("deviceName", "Living Room Sensor");

// Device reads on boot
String name = iot.getString("deviceName", "Unnamed Device");
```

---

### 3. **TL_BLE** - Bluetooth Low Energy Provisioning

**Purpose**: WiFi provisioning via mobile app when no credentials are saved

**BLE Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

**Characteristics**:
1. **WiFi Characteristic** (Write):
   - UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
   - Receives WiFi credentials from app
   - Format: `{"ssid": "WiFiName", "password": "password123"}`

2. **Status Characteristic** (Read + Notify):
   - UUID: `cba1d466-344c-4be3-ab3f-189f80dd7518`
   - Sends connection status to app
   - Format: `{"status": "connected", "ip": "192.168.1.100"}`

**Key Functions**:
```cpp
bool startBLE(const char* deviceName);
void stopBLE();
bool isBLEActive();
void onBLECredentialsReceived(void (*callback)(String ssid, String password));
void sendBLEStatus(bool connected, const String& ip);
```

**Flow**:
1. Device starts BLE advertising with name `ThingsLinker_<ChipID>`
2. App scans and connects to BLE device
3. App writes WiFi credentials to WiFi characteristic
4. Device receives credentials via callback
5. Device connects to WiFi
6. Device sends status back via status characteristic
7. Device stops BLE after successful WiFi connection

---

### 4. **TL_WiFi** - WiFi Connection Management

**Purpose**: Connect to WiFi and manage credentials

**Namespace**: `"thingslinker"` (separate from app storage)

**Key Functions**:
```cpp
bool connectWiFi(const char* ssid, const char* password, bool saveCredentials = true);
bool connectWiFi();  // Use saved credentials
void disconnectWiFi();
bool isWiFiConnected();
void clearWiFiCredentials();
bool hasWiFiCredentials();
String getWiFiIP();
int getWiFiRSSI();
```

**Saved Data** (in flash):
- `ssid` (String)
- `password` (String)
- `saved` (bool flag)

**Connection Process**:
1. Set WiFi mode to Station (STA)
2. Call `WiFi.begin(ssid, password)`
3. Wait up to 30 seconds (WIFI_CONNECT_TIMEOUT)
4. If successful:
   - Print IP address
   - Print signal strength (RSSI)
   - Save credentials if requested
5. If failed:
   - Return false
   - Trigger BLE provisioning

---

### 5. **TL_MQTT** - MQTT Client & Topic Management

**Purpose**: MQTT communication with ThingsLinker backend

**Dependencies**:
- `PubSubClient` library
- `ArduinoJson` library

**Global State**:
```cpp
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static String authToken = "";
static String blueprintId = "";
static PinCallback pinCallbacks[125];  // V0-V124
static int pinCallbackCount = 0;
```

**Key Functions**:
```cpp
bool connectMQTT(const char* authToken, const char* blueprintId,
                 const char* mqttUsername, const char* mqttPassword);
void disconnectMQTT();
bool isMQTTConnected();
void subscribeMQTT(const char* widgetType, const char* pin,
                   void (*callback)(float value));
void publishMQTT(const char* widgetType, const char* pin, float value);
void loopMQTT();
String getChipID();
```

**Topic Format**:
```
device/{WidgetType}/{BlueprintId}/{AuthToken}/{VirtualPin}/
```

**Example Topics**:
```
device/Button/YOUR_BLUEPRINT_ID/YOUR_AUTH_TOKEN/V0/
device/Gauge/YOUR_BLUEPRINT_ID/YOUR_AUTH_TOKEN/V1/
```

**Client ID Generation**:
```cpp
String clientId = "ThingsLinker_" + getChipID();   // GSM: "TLG_" + chip ID
// Example: ThingsLinker_A8032AB123CD
```

**Authentication**:
- The ThingsLinker broker accepts devices without a login (`iot.begin()`).
- Optional: `iot.begin("MQTT_USERNAME", "MQTT_PASSWORD")` sends a username and
  password, for a broker of your own that asks for one. Both must be non-empty,
  otherwise the library connects without a login.

**Payload Format**:

Device → Cloud (Telemetry):
```json
{
  "v": 25.5,
  "t": 1234567890
}
```

Cloud → Device (Control):
```json
{
  "v": 1
}
```

**Pin Callback System**:
```cpp
struct PinCallback {
  String pin;          // "V0", "V1", etc.
  String widgetType;   // "Button", "Gauge", etc.
  void (*callback)(float);
};
```

---

### 6. **ThingsLinker** - Main Wrapper Class

**Purpose**: User-facing API that orchestrates all components

**Private Members**:
```cpp
const char* _authToken;
const char* _blueprintId;
const char* _mqttUsername;
const char* _mqttPassword;
bool _debugEnabled;
bool _initialized;
unsigned long _lastCheck;
```

**Public API**:

#### Initialization:
```cpp
ThingsLinker(const char* authToken, const char* blueprintId);
void begin(const char* mqttUsername, const char* mqttPassword);
void run();
```

#### Widget Functions (Device → App):
```cpp
void gauge  (const char* pin, float value);  // "Gauge" widget
void chart  (const char* pin, float value);  // "Chart" widget (telemetry history)
void display(const char* pin, float value);  // "Value Display" widget
void send   (const char* pin, float value);  // alias for display()
void label  (const char* pin, float value);  // "Label" widget
void led    (const char* pin, bool value);   // "LED" widget (indicator)
void map    (const char* pin, float lat, float lng); // "Map" widget
```

#### Widget Callbacks (App → Device):
```cpp
void onButton  (const char* pin, void (*callback)(bool pressed));
void onSwitch  (const char* pin, void (*callback)(bool on));
void onSlider  (const char* pin, void (*callback)(float value));
void onRGB     (const char* pin, void (*callback)(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern));
void onLED     (const char* pin, void (*callback)(bool on));
void onTimer   (const char* pin, void (*callback)(float seconds));
void onJoystick(const char* pin, void (*callback)(float x, float y));
```

#### Status Functions:
```cpp
bool wifiConnected();
bool mqttConnected();
String getIP();
String getChipID();
```

#### Storage Functions (NVS — survives reboot):
```cpp
// Keys max 15 characters; namespace: "tl_app"
bool   saveString(const char* key, const String& value);
String getString (const char* key, const String& defaultValue);
bool   saveInt   (const char* key, int value);
int    getInt    (const char* key, int defaultValue);
bool   saveFloat (const char* key, float value);
float  getFloat  (const char* key, float defaultValue);
bool   saveBool  (const char* key, bool value);
bool   getBool   (const char* key, bool defaultValue);
bool   hasKey    (const char* key);
bool   removeKey (const char* key);
void   clearAllData();
```

#### Advanced Functions:
```cpp
void resetWiFi();                     // Clear WiFi credentials → restart BLE provisioning
void debug(bool enable);              // Enable/disable verbose Serial logs
void setBLEName(const char* name);    // Override default BLE name (call before begin())
```

---

## Step-by-Step Flow

### 🚀 **Step 1: Initialization**

**User Code**:
```cpp
#include <ThingsLinker.h>

ThingsLinker iot("YOUR_AUTH_TOKEN", "YOUR_BLUEPRINT_ID");

void setup() {
  iot.begin();   // optional: iot.begin("MQTT_USERNAME", "MQTT_PASSWORD")
}
```

**What Happens**:
1. Constructor stores `authToken` and `blueprintId`
2. `begin()` is called in `setup()`
3. Stores the optional `mqttUsername` and `mqttPassword` (none by default)
4. Initializes Serial @ 115200 baud
5. Prints banner with Chip ID
6. Checks for saved WiFi credentials

---

### 🔍 **Step 2A: WiFi Credentials Found (Normal Boot)**

**Flow**:
```
begin()
  → hasWiFiCredentials() [returns true]
  → connectWiFi() [use saved credentials]
    → WiFi.begin(ssid, password)
    → Wait up to 30 seconds
    → Success!
  → connectMQTT(authToken, blueprintId, mqttUsername, mqttPassword)
    → Set MQTT server and callback
    → Generate unique clientId
    → Connect with authentication
    → Success!
  → Device ready!
```

**Serial Output**:
```
========================================
   ThingsLinker IoT Library v2.0
========================================
Chip ID : A8032AB123CD
Broker  : mqtt.thingslinker.com:8883
========================================

[Setup] Saved WiFi found, connecting...
[WiFi] ✓ Connected! IP: 192.168.1.100  RSSI: -45 dBm
[WiFi] NTP sync started (pool.ntp.org)
[MQTT] Connecting to mqtt.thingslinker.com:8883
[MQTT] ✓ Connected!
[MQTT] ✓ Status → ONLINE
[Setup] ✓ MQTT connected — device ready!
```

---

### 🔍 **Step 2B: No WiFi Credentials (First Boot or Reset)**

**Flow**:
```
begin()
  → hasWiFiCredentials() [returns false]
  → handleBLEProvisioning()
    → Build device name: "ThingsLinker_" + getChipID()
    → Set BLE callback for credentials
    → startBLE(deviceName)
      → BLEDevice::init(deviceName)
      → Create BLE Server
      → Create BLE Service (UUID: 4fafc201...)
      → Create WiFi Characteristic (write)
      → Create Status Characteristic (read + notify)
      → Start advertising
    → Print instructions
```

**Serial Output**:
```
========================================
   ThingsLinker IoT Library v2.0
========================================
Chip ID : A8032AB123CD
Broker  : mqtt.thingslinker.com:8883
========================================

[Setup] No WiFi saved — starting BLE provisioning...
[BLE] Starting BLE: ThingsLinker_A8032AB123CD
[BLE] ✓ Advertising started
[BLE] Open ThingsLinker app → scan → select ThingsLinker_A8032AB123CD → enter WiFi
```

---

### 📱 **Step 3: BLE Provisioning (User Interaction)**

**Mobile App Flow**:
1. User opens ThingsLinker app
2. App scans for BLE devices
3. Finds `ThingsLinker_A8032AB123CD`
4. User selects device and taps "Connect"
5. App prompts for WiFi credentials
6. User enters SSID and password
7. App sends JSON via BLE:
   ```json
   {"ssid": "MyNetwork", "password": "MyPassword123"}
   ```

**Device Flow**:
```
BLE Callback triggered
  → Parse JSON
  → Extract ssid and password
  → Call credentialsCallback(ssid, password)
    → connectWiFi(ssid, password, true)
      → WiFi.begin()
      → Wait for connection
      → Save credentials to flash
      → sendBLEStatus(true, "192.168.1.100")
        → Send via status characteristic
        → App receives success notification
      → Delay 3 seconds
      → stopBLE()
        → Stop advertising
        → Deinit BLE
    → connectMQTT(authToken, blueprintId, mqttUsername, mqttPassword)
      → MQTT connects successfully
```

**Serial Output**:
```
[BLE] App connected
[BLE] Received credentials — SSID: MyNetwork
[WiFi] Connecting to MyNetwork...
[WiFi] ✓ Connected! IP: 192.168.1.100  RSSI: -52 dBm
[WiFi] ✓ Credentials saved to flash
[BLE] ✓ Notified app — stopping BLE...
[MQTT] Connecting to mqtt.thingslinker.com:8883
[MQTT] ✓ Connected!
[MQTT] ✓ Status → ONLINE
[Setup] ✓ MQTT connected — device ready!
```

---

### 🔄 **Step 4: Main Loop Execution**

**User Code**:
```cpp
void loop() {
  iot.run();

  // User code here
  float temp = readTemperature();
  iot.gauge("V1", temp);

  delay(5000);
}
```

**What `iot.run()` Does**:
```cpp
void ThingsLinker::run() {
  if (!_initialized) return;

  // 1. Process MQTT messages (incoming widget controls)
  loopMQTT();

  // 2. Check connections every 5 seconds
  unsigned long now = millis();
  if (now - _lastCheck > 5000) {
    _lastCheck = now;
    checkConnections();
  }
}
```

**checkConnections() Logic**:
```cpp
void ThingsLinker::checkConnections() {
  // Check WiFi
  if (!isWiFiConnected()) {
    Serial.println("[Check] WiFi disconnected");
    if (hasWiFiCredentials()) {
      Serial.println("[Check] Reconnecting WiFi...");
      connectWiFi();
    }
  }

  // Check MQTT
  if (isWiFiConnected() && !isMQTTConnected()) {
    Serial.println("[Check] MQTT disconnected");
    Serial.println("[Check] Reconnecting MQTT...");
    connectMQTT(_authToken, _blueprintId, _mqttUsername, _mqttPassword);
  }
}
```

---

### 📤 **Step 5: Publishing Data (Device → Cloud)**

**User Code**:
```cpp
float temperature = 25.5;
iot.gauge("V1", temperature);
```

**Execution Flow**:
```cpp
gauge("V1", 25.5)
  → publishMQTT("Gauge", "V1", 25.5)
    → Build topic:
        "device/Gauge/YOUR_BLUEPRINT_ID/YOUR_AUTH_TOKEN/V1/"
    → Build payload:
        {"v": 25.5, "t": 1234567890}
    → mqttClient.publish(topic, payload)
    → Print success/failure
```

**Serial Output**:
```
[MQTT] ✓ V1 = 25.50
```

**What Happens in Backend**:
1. MQTT broker receives message on topic
2. Backend parses topic to extract:
   - Widget Type: `Gauge`
   - Blueprint ID: `YOUR_BLUEPRINT_ID`
   - Auth Token: `YOUR_AUTH_TOKEN`
   - Virtual Pin: `V1`
3. Backend validates device authentication
4. Backend saves telemetry data to database
5. Backend forwards to connected mobile apps via WebSocket/MQTT

---

### 📥 **Step 6: Receiving Commands (Cloud → Device)**

**User Sets Up Callback**:
```cpp
void setup() {
  iot.begin(...);

  iot.onButton("V0", [](bool value) {
    digitalWrite(LED_PIN, value ? HIGH : LOW);
    Serial.println(value ? "LED ON" : "LED OFF");
  });
}
```

**What Happens**:
```cpp
onButton("V0", callback)
  → subscribeMQTT("Button", "V0", internalCallback)
    → Save callback to pinCallbacks array:
        pinCallbacks[0] = {
          pin: "V0",
          widgetType: "Button",
          callback: wrappedCallback
        }
    → Build topic:
        "device/Button/YOUR_BLUEPRINT_ID/YOUR_AUTH_TOKEN/V0/"
    → mqttClient.subscribe(topic, QoS 1)
```

**Serial Output**:
```
[MQTT] ✓ Subscribed: device/Button/YOUR_BLUEPRINT_ID/YOUR_AUTH_TOKEN/V0/
```

**When User Presses Button in App**:
```
App publishes to topic (retained):
  device/Button/YOUR_BLUEPRINT_ID/YOUR_AUTH_TOKEN/V0/

Payload: {"v": 1, "t": 1234567890}

Device receives message:
  → mqttCallback() triggered
    → Parse JSON
    → Extract value: 1 → true (bool)
    → Extract pin from topic: "V0"
    → Find matching callback in pinCallbacks array
    → Call user's callback: onButton(true)
      → digitalWrite(LED_PIN, HIGH)
      → Serial.println("LED ON")
```

**Serial Output**:
```
[MQTT] ✓ Button V0 = 1
LED ON
```

---

## Complete Execution Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 Powers On                          │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  setup() → iot.begin()                                      │
│    • Initialize Serial @ 115200                             │
│    • Print banner with Chip ID                              │
│    • Store optional MQTT username/password (none by default)│
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  Check: hasWiFiCredentials()?                               │
└────────────┬───────────────────────────┬────────────────────┘
             │ YES                       │ NO
             ▼                           ▼
┌──────────────────────────┐  ┌──────────────────────────────┐
│ connectWiFi()            │  │ handleBLEProvisioning()      │
│  • Load saved SSID/pass  │  │  • Build device name         │
│  • WiFi.begin()          │  │  • startBLE()                │
│  • Wait 30 seconds       │  │  • Print instructions        │
│  • Success!              │  │  • Wait for app connection   │
└────────┬─────────────────┘  └─────────┬────────────────────┘
         │                              │
         │                              ▼
         │                    ┌──────────────────────────────┐
         │                    │ App sends WiFi credentials   │
         │                    │  • Receive via BLE           │
         │                    │  • connectWiFi(ssid, pass)   │
         │                    │  • Save to flash             │
         │                    │  • sendBLEStatus()           │
         │                    │  • stopBLE()                 │
         │                    └─────────┬────────────────────┘
         │                              │
         └──────────────┬───────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────────┐
│  WiFi Connected!                                            │
│    • Print IP address                                       │
│    • Print signal strength                                  │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  connectMQTT(authToken, blueprintId, mqttUser, mqttPass)    │
│    • Set MQTT server: mqtt.thingslinker.com:8883            │
│    • Client ID: "ThingsLinker_" + ChipID                    │
│    • Connect (with username/password only if given)         │
│    • Set callback for incoming messages                     │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  MQTT Connected! Device Ready!                              │
│    • _initialized = true                                    │
│    • Ready to send/receive widget data                      │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                    loop() → iot.run()                       │
│                                                             │
│  Every cycle:                                               │
│    • loopMQTT() → Process incoming MQTT messages            │
│    • Execute user code                                      │
│                                                             │
│  Every 5 seconds:                                           │
│    • checkConnections()                                     │
│      - Reconnect WiFi if disconnected                       │
│      - Reconnect MQTT if disconnected                       │
└─────────────────────────────────────────────────────────────┘
```

---

## Widget Communication Flow

### Sending Data (Device → Cloud → App)

```
User Code:                  ThingsLinker:                MQTT:                    Backend:
───────────                 ─────────────                ─────                    ────────
iot.gauge("V1", 25.5)  →   publishMQTT()           →   Publish                →  Receive
                            • Widget: "Gauge"            Topic: device/          • Parse topic
                            • Build topic                Gauge/.../V1/           • Validate device
                            • Build payload:             Payload:                • Save to DB
                              {"v":25.5,"t":...}         {"v":25.5,...}          • Forward to apps
                            • Publish to MQTT

// Functions: gauge(), chart(), display(), send(), label(), led(), map()
// All build topic as: device/{Type}/{blueprintId}/{authToken}/{pin}/
// Payload always: {"v": float, "t": unix_timestamp}
// RGB full payload: {"v":1,"r":0,"g":255,"b":204,"status":"ON","count":50,"pattern":"Solid","t":ts}
```

### Receiving Commands (App → Cloud → Device)

```
Mobile App:                 Backend:                     MQTT:                   Device:
───────────                 ────────                     ─────                   ───────
User presses button    →   Forward to MQTT          →   Publish              →  mqttCallback()
• Button widget V0          Topic: device/              Topic: device/          • Parse JSON
• Value: ON (1)             Button/.../V0/              Button/.../V0/          • Extract pin: V0
                            Payload: {"v":1}            Payload: {"v":1}        • Find callback
                                                                                 • Call user lambda
                                                                                 • Execute action
```

---

## Storage System

### Two Separate Namespaces

| Namespace | Purpose | Data Stored |
|-----------|---------|-------------|
| `"thingslinker"` | WiFi credentials | ssid, password, saved flag |
| `"tl_app"` | App data | User-defined key-value pairs (max key: 15 chars) |

### Usage Examples

#### Storing Device Configuration from App:
```cpp
// App sends device name via special widget
iot.onValue("V10", [](float value) {
  // Assuming value encodes a command
  String deviceName = "Kitchen Sensor";
  iot.saveString("deviceName", deviceName);
});

// On boot, retrieve and use
void setup() {
  iot.begin(...);
  String name = iot.getString("deviceName", "Unnamed Device");
  Serial.println("Device: " + name);
}
```

#### Storing Threshold from App:
```cpp
// App sends threshold via slider
iot.onSlider("V11", [](float value) {
  iot.saveInt("threshold", (int)value);
  Serial.println("Threshold saved: " + String((int)value));
});

// Use in logic
void loop() {
  iot.run();

  float temp = readTemperature();
  int threshold = iot.getInt("threshold", 30);

  if (temp > threshold) {
    activateAlarm();
  }
}
```

---

## Error Handling & Recovery

### WiFi Connection Failure

**Scenario**: WiFi connection times out after 30 seconds

**Behavior**:
```cpp
connectWiFi(ssid, password)
  → Wait 30 seconds
  → Still not connected
  → Return false
  → Trigger BLE provisioning
```

**User Experience**:
- Device starts BLE advertising
- User re-enters WiFi credentials via app
- Device tries again

### MQTT Connection Failure

**Scenario**: MQTT broker unreachable or credentials invalid

**Behavior**:
```cpp
connectMQTT(...)
  → mqttClient.connect()
  → Returns false
  → Print state code (e.g., -2 = connection failed)
  → checkConnections() retries every 5 seconds
```

**MQTT State Codes**:
- `-4`: Connection timeout
- `-3`: Connection lost
- `-2`: Connect failed
- `-1`: Disconnected
- `0`: Connected

### Auto-Reconnection

**WiFi Auto-Reconnect**:
```cpp
checkConnections() [called every 5 seconds]
  → if (!isWiFiConnected())
      → connectWiFi() // Use saved credentials
```

**MQTT Auto-Reconnect**:
```cpp
checkConnections() [called every 5 seconds]
  → if (WiFi connected && !MQTT connected)
      → connectMQTT(...)
```

---

## Memory Management

### Stack Usage

**Global Static Variables**:
- WiFi client: ~100 bytes
- MQTT client: ~500 bytes
- Pin callbacks array: 125 × 32 bytes = 4KB

**Heap Usage**:
- BLE Server (when active): ~10KB
- JSON parsing: ~512 bytes per document
- String operations: Dynamic

### Flash Usage

**Preferences Storage**:
- WiFi namespace: ~128 bytes
- App namespace: Up to 4KB total (user-defined)

---

## Security Considerations

### Credentials Storage

**✅ Secure**:
- Credentials stored in ESP32 NVS (encrypted flash)
- Not accessible via serial output (passwords not printed)

**⚠️ Caution**:
- BLE communication is unencrypted during provisioning
- Use in trusted environment for initial setup

### MQTT Authentication

- The ThingsLinker broker (`mqtt.thingslinker.com:8883`, TLS) accepts devices
  without a username or password, so `iot.begin()` needs no arguments.
- MQTT username and password are optional. Pass them only for a broker of your
  own that requires a login: `iot.begin("MQTT_USERNAME", "MQTT_PASSWORD")`
  (GSM: `iot.begin("MQTT_USERNAME", "MQTT_PASSWORD", APN, pwrPin)`).

---

## Debugging Tips

### Enable Debug Logs

**Current State**: Debug logs always enabled

**Serial Output Shows**:
- `[WiFi]` prefix for WiFi operations
- `[BLE]` prefix for BLE operations
- `[MQTT]` prefix for MQTT operations
- `[Setup]` prefix for initialization
- `[Check]` prefix for connection checks

### Common Issues

**Issue**: Device not connecting to WiFi
- **Check**: Correct SSID and password
- **Check**: 2.4GHz network (ESP32 doesn't support 5GHz)
- **Check**: WiFi signal strength

**Issue**: MQTT connection fails (State: -2)
- **Check**: Internet connectivity
- **Check**: MQTT server reachable (ping mqtt.thingslinker.com)
- **Check**: No MQTT username/password passed (the ThingsLinker broker needs none)
- **Check**: Auth token and blueprint ID valid

**Issue**: Callbacks not triggering
- **Check**: Topic subscription successful
- **Check**: Widget type matches (Button, Slider, etc.)
- **Check**: Virtual pin matches (V0, V1, etc.)
- **Check**: `iot.run()` called in loop

---

## 🎉 Summary

### Library Design Principles

1. **Simplicity**: 3-line setup (create, begin, run)
2. **Automatic**: BLE, WiFi, MQTT handled automatically
3. **Modular**: Separate files for each component
4. **Persistent**: WiFi credentials and app data survive reboots
5. **Reliable**: Auto-reconnection for WiFi and MQTT
6. **Scalable**: 125 virtual pins (V0-V124)
7. **Flexible**: Works with both production and test servers

### Key Features

✅ BLE provisioning for easy WiFi setup
✅ Persistent storage (ESP32 NVS — namespace `"tl_app"`)
✅ Auto-reconnection (WiFi + MQTT)
✅ 125 virtual pins support (V0–V124)
✅ Widget-based communication (12 widget types)
✅ JSON payloads (`{"v":float,"t":unix}`)
✅ Unique MQTT topics per device
✅ Retained MQTT messages (state restored on reconnect)
✅ RGB with LED count parameter
✅ NTP time sync (pool.ntp.org)
✅ Customizable BLE device name (`setBLEName()`)
✅ TLS/SSL MQTT connection (port 8883)

### Typical User Experience

**First Boot**:
1. Power on device
2. Open app and scan BLE
3. Enter WiFi credentials
4. Device connects automatically
5. Start controlling from app!

**Every Boot After**:
1. Power on device
2. Auto-connects to WiFi (saved)
3. Auto-connects to MQTT
4. Ready in seconds!

---

**Generated**: 2025-11-12 | **Updated**: 2025-07-30
**Version**: 2.0
**Status**: ✅ Complete Step-by-Step Architecture Documentation
