/**
 * @file       ThingsLinker.h
 * @author     Chetan Mahajan
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2021 Chetan Mahajan
 * @date       April 2021
 * @brief
 *
 */

#ifndef ThingsLinker_H
#define ThingsLinker_H

#include <Arduino.h>
#include <functional>
#include "Config/Config.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error "Unsupported platform"
#endif

class ThingsLinker
{
public:
  ThingsLinker() = default;
  explicit ThingsLinker(const String &pin);

  void begin(const char *ssid, const char *pass, const char *authToken);

  void setCallback(std::function<void()> cb);
  static void callbackMqtt(char *topic, byte *payload, unsigned int length);
  void connectToWiFi(const char *ssid, const char *pass);
  static void connectToMQTT(const char *authToken);
  void subscribePin(const char *pin);
  static void taskESP32(void *pvParameters);
  void loop();

  int getDeviceStatus(const String &pin);
  void publishMQTTMessage(const String &pin, float sensorValue);

  int getButtonAsInt(const String &pin);

  int getTimerAsInt(const String &pin);

  int getSliderAsInt(const String &pin);

  void putTimer(const String &pin, const String &timerStatus);

  void setGauge(const String &pin, float sensorValue);

  void setDisplay(const String &pin, float sensorValue);

  static void updateMap(const String &pin, int sensorValue);

private:
  const char *ssid{nullptr};
  const char *pass{nullptr};
  const char *authToken{nullptr};

  const String pin{};

  String timerStatus{};
  float sensorValue{};

  char *topic{nullptr};
  byte *payload{nullptr};
  unsigned int length{0};

  static std::function<void()> _callback;

  void connectToWifi();

  void connectToMqtt();
};

#endif
