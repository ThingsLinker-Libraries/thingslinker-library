/**
 * @file TL_MQTT.h
 * @brief MQTT communication - publish and subscribe to widgets
 *
 * This file handles ALL MQTT operations:
 * - Connect to MQTT server
 * - Subscribe to control topics
 * - Publish sensor data
 * - Auto-reconnect
 *
 * Super simple to use!
 */

#ifndef TL_MQTT_H
#define TL_MQTT_H

#include <Arduino.h>
#include "TL_Value.h"

/**
 * @brief Connect to MQTT server
 * @param authToken Device authentication token
 * @param blueprintId Device blueprint ID
 * @param mqttUsername Optional broker username (nullptr or "" connects without a login)
 * @param mqttPassword Optional broker password (nullptr or "" connects without a login)
 * @return true if connected
 *
 * Example:
 *   connectMQTT("YOUR_AUTH_TOKEN", "YOUR_BLUEPRINT_KEY", nullptr, nullptr);
 */
bool connectMQTT(const char* authToken, const char* blueprintId, const char* mqttUsername, const char* mqttPassword);

/**
 * @brief Disconnect from MQTT
 *
 * Example:
 *   disconnectMQTT();
 */
void disconnectMQTT();

/**
 * @brief Check if MQTT is connected
 * @return true if connected
 *
 * Example:
 *   if (isMQTTConnected()) {
 *     Serial.println("MQTT Online!");
 *   }
 */
bool isMQTTConnected();

/**
 * @brief Subscribe to a pin (to receive control from app)
 * @param widgetType Widget type (Button, Switch, Slider, Gauge, etc.)
 * @param pin Virtual pin (V0 to V124 supported - 125 pins total)
 * @param callback Function to call when value received
 *
 * Example:
 *   subscribeMQTT("Button", "V0", [](float value) {
 *     Serial.println("Button: " + String(value));
 *   });
 */
void subscribeMQTT(const char* widgetType, const char* pin, void (*callback)(float value));

/**
 * @brief Subscribe to button widget (receives bool value)
 * @param widgetType Widget type (usually "Button")
 * @param pin Virtual pin (V0 to V124)
 * @param callback Function to call with bool value
 *
 * Example:
 *   subscribeMQTTButton("Button", "V0", [](bool pressed) {
 *     digitalWrite(LED_PIN, pressed ? HIGH : LOW);
 *   });
 */
void subscribeMQTTButton(const char* widgetType, const char* pin, void (*callback)(bool value));

/**
 * @brief Subscribe to an RGB widget.
 * Callback receives:
 *   r, g, b  — color channels (0–255)
 *   on       — strip on/off (v > 0)
 *   count    — number of LEDs to light (1–300, from app)
 *   pattern  — effect name (e.g. "Solid", "Blink") or "" if not sent
 *
 * Payload: {"v":1,"r":0,"g":255,"b":204,"status":"ON","count":50,"pattern":"Solid","t":...}
 *
 * Example:
 *   subscribeRGBMQTT("V6", [](uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern) {
 *     strip.clear();
 *     if (on) strip.fill(strip.Color(r, g, b), 0, count);
 *     strip.show();
 *     Serial.printf("Pattern: %s\n", pattern);
 *   });
 */
void subscribeRGBMQTT(const char* pin, void (*callback)(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count, const char* pattern));

/**
 * @brief Subscribe to a Joystick widget.
 * Callback receives x and y axis values (typically -1.0 to 1.0).
 * Use this instead of onValue() for Joystick widgets.
 *
 * Example:
 *   subscribeJoystickMQTT("V13", [](float x, float y) {
 *     Serial.printf("X:%.2f  Y:%.2f\n", x, y);
 *   });
 */
void subscribeJoystickMQTT(const char* pin, void (*callback)(float x, float y));

/**
 * @brief Subscribe to a text widget (receives the message as a string)
 * @param widgetType Widget type (usually "Terminal")
 * @param pin Virtual pin (V0 to V124)
 * @param callback Function to call with the received text. The pointer is
 *                 only valid during the callback; copy it to keep it.
 *
 * Example:
 *   subscribeMQTTText("Terminal", "V5", [](const char* text) {
 *     Serial.println(text);
 *   });
 */
void subscribeMQTTText(const char* widgetType, const char* pin, void (*callback)(const char* text));

/**
 * @brief Publish value to a pin (send sensor data to app)
 * @param widgetType Widget type (Button, Switch, Slider, Gauge, etc.)
 * @param pin Virtual pin (V0 to V124 supported - 125 pins total)
 * @param value Value to send. The data type follows the argument type:
 *              int → Integer, float → Float, bool → Boolean, text → String.
 *
 * Example:
 *   publishMQTT("Gauge", "V1", 25.5);          // Float
 *   publishMQTT("LED", "V2", true);            // Boolean
 *   publishMQTT("Label", "V3", "Door open");   // String
 */
void publishMQTT(const char* widgetType, const char* pin, const TLValue& value);

/**
 * @brief Publish GPS coordinates to a Map widget.
 * Sends {"v": lat, "lat": lat, "lng": lng, "t": timestamp} so the
 * app can extract both coordinates from the payload.
 *
 * @param pin Virtual pin (V0 to V124)
 * @param lat Latitude  (decimal degrees, e.g. 23.0225)
 * @param lng Longitude (decimal degrees, e.g. 72.5714)
 *
 * Example:
 *   publishMQTTMap("V12", 23.0225f, 72.5714f);
 */
void publishMQTTMap(const char* pin, float lat, float lng);

/**
 * @brief Process MQTT messages (call in loop)
 *
 * Example:
 *   void loop() {
 *     loopMQTT();
 *   }
 */
void loopMQTT();

/**
 * @brief Get device chip ID
 * @return Chip ID as hex string
 *
 * Example:
 *   String id = getChipID();
 *   Serial.println("Chip ID: " + id);
 */
String getChipID();

#endif // TL_MQTT_H
