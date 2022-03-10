/**
 * @file       ThingsLinker.cpp
 * @author     Chetan Mahajan
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2021 Chetan Mahajan
 * @date       April 2021
 * @brief
 *
 */

#include "ThingsLinker.h"
#include "Config/Config.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

Config config;
int deviceStatus = 0;
HTTPClient http;
WiFiClient client;
ThingsLinker::ThingsLinker()
{
}
ThingsLinker::ThingsLinker(const char *ssid, const char *pass)
{
  this->ssid = ssid;
  this->pass = pass;
  setWifi();
}

void ThingsLinker::setWifi()
{
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting...");
  }
  Serial.println(".......");
  Serial.println("WiFi Connected....IP Address:");
  Serial.println(WiFi.localIP());
}

void ThingsLinker::run(String authToken)
{

  StaticJsonDocument<200> jsonBuffer;
  JsonObject root = jsonBuffer.as<JsonObject>();

  http.begin(client, config.BASH_URL + config.ENDPOINT + authToken);
  http.addHeader(config.CONTENTTYPE, config.CONTENTTYPEVALUE);
  int httpCode = http.GET();
  if (httpCode == 200)
  {
    // Serial.println("Success");
    // Serial.println(httpCode);
  }
  else
  {
    Serial.println("Fail");
    Serial.println(httpCode);
  }
}

int ThingsLinker::getOnOff(String pin)
{
  DynamicJsonDocument root(1536 * 2);
  DeserializationError error = deserializeJson(root, http.getString());
  if (error)
  {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
  }
  String body = root[config.body];
  deserializeJson(root, body);
  String currentTime = root[config.currentTime];
  JsonArray jsonArray = root.as<JsonArray>();
  for (int i = 0; i < jsonArray.size(); i++)
  {
    JsonObject jsonObject = jsonArray[i];
    String devicePin = jsonObject[config.devicePin];
    String deviceType = jsonObject[config.deviceType];
    if (deviceType == "Button")
    {
      if (devicePin == pin)
      {
        deviceStatus = jsonObject[config.deviceStatus];
      }
    }
  }

  return deviceStatus;
}

int ThingsLinker::getTimerOnOff(String pin)
{
  DynamicJsonDocument root(1536 * 2);
  DeserializationError error = deserializeJson(root, http.getString());
  if (error)
  {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
  }
  String currentTime = root[config.currentTime];
  String body = root[config.body];
  deserializeJson(root, body);
  JsonArray jsonArray = root.as<JsonArray>();
  for (int i = 0; i < jsonArray.size(); i++)
  {
    JsonObject jsonObject = jsonArray[i];
    String devicePin = jsonObject[config.devicePin];
    String deviceType = jsonObject[config.deviceType];
    String startTime = jsonObject[config.startTime];
    String stopTime = jsonObject[config.stopTime];
    String id = jsonObject[config._id];

    Serial.println("Current time1 " + currentTime);
    // Serial.println(startTime);

    if (deviceType == "Timer")
    {
      if (devicePin == pin)
      {
        deviceStatus = jsonObject[config.deviceStatus];
        if (deviceStatus == 0)
        {
          if (currentTime == startTime)
          {
            putTimer(id, "1");
          }
        }
        else
        {
          if (currentTime == stopTime)
          {
            putTimer(id, "0");
          }
        }
      }
    }
  }
  return deviceStatus;
}

void ThingsLinker::putTimer(String id, String timerStatus)
{
  String postData = "&" + config.deviceStatus + "=" + timerStatus;
  http.begin(client, config.BASH_URL + config.DEVICEENDPOINT + id);
  http.addHeader(config.CONTENTTYPE, config.CONTENTTYPEVALUE);
  int httpCode = http.PUT(postData);
  String payload = http.getString();
  if (httpCode == 200)
  {
    // Serial.println("Success");
    // Serial.println(httpCode);
  }
  else
  {
    Serial.println("Fail");
    Serial.println(httpCode);
  }
}

int ThingsLinker::getSlider(String pin)
{
  DynamicJsonDocument root(1536 * 2);
  DeserializationError error = deserializeJson(root, http.getString());
  if (error)
  {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
  }
  String body = root[config.body];
  deserializeJson(root, body);
  JsonArray jsonArray = root.as<JsonArray>();
  for (int i = 0; i < jsonArray.size(); i++)
  {
    JsonObject jsonObject = jsonArray[i];
    String devicePin = jsonObject[config.devicePin];
    String deviceType = jsonObject[config.deviceType];
    if (deviceType == "Slider")
    {
      if (devicePin == pin)
      {
        deviceStatus = jsonObject[config.deviceStatus];
      }
    }
  }
  return deviceStatus;
}

void ThingsLinker::setGauge(String pin, float sensorValue)
{
  delay(2000);
  DynamicJsonDocument root(1536 * 2);
  DeserializationError error = deserializeJson(root, http.getString());
  if (error)
  {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
  }
  String body = root[config.body];
  deserializeJson(root, body);
  JsonArray jsonArray = root.as<JsonArray>();
  for (int i = 0; i < jsonArray.size(); i++)
  {
    JsonObject jsonObject = jsonArray[i];
    String devicePin = jsonObject[config.devicePin];
    String deviceType = jsonObject[config.deviceType];
    String id = jsonObject[config._id];
    if (deviceType == "Gauge")
    {
      if (devicePin == pin)
      {
        String postData = "&" + config.deviceStatus + "=" + sensorValue;
        http.begin(client, config.BASH_URL + config.DEVICEENDPOINT + id);
        http.addHeader(config.CONTENTTYPE, config.CONTENTTYPEVALUE);
        int httpCode = http.PUT(postData);
        String payload = http.getString();
        if (httpCode == 200)
        {
          Serial.println("Sensor success");
          Serial.println(httpCode);
        }
        else
        {
          Serial.println("Sensor fail");
          Serial.println(httpCode);
        }
      }
    }
  }
}
