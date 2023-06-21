/**
 * @file       Config.h
 * @author     Chetan Mahajan
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2021 Chetan Mahajan
 * @date       April 2021
 * @brief
 *
 */

#define CONFIG_H
#include <Arduino.h>
#ifdef CONFIG_H

#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error "Unsupported platform"
#endif
class Config
{
private:
public:
    Config();
    static const char *MQTTSERVER;
    static const int MQTTPORT;
    static const char *TOPIC;
    static const char *devicePin;
    static const char *deviceStatus;
    static const char *eventCode;
    static const char *event;
    static const char *charts;
    String getESP32ChipId();
    String getESP8266ChipId();
    String base64_encode(String str);
    String base64Decode(String base64Encoded);
    int countPadding(String base64Encoded);
};

#endif