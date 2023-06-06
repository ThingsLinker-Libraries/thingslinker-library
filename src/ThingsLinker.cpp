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
#include <Arduino.h>

std::function<void()> ThingsLinker::_callback = []() {};
int mappingStatus[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
String pinArray[] = {"V0", "V1", "V2", "V3", "V4", "V5", "V6", "V7", "V8", "V9", "V10", "V11", "V12", "V13", "V14", "V15", "V16", "V17", "V18", "V19", "V20", "V21", "V22", "V23", "V24"};

Config config;
int deviceStatus = -1;
String mqttEncodedPayload = "";
const char *auth_token = "";
String mqttCllent = "";
String statusTopic = "";

WiFiClient espClient;
PubSubClient client(espClient);

ThingsLinker Things;

String myPinStrings[25];
int incrementPin = 0;

void ThingsLinker::begin(const char *ssid, const char *pass, const char *authToken)
{
  Serial.println();
  Serial.println("  _______ _     _                 _ _       _             \n |__   __| |   (_)               | (_)     | |            \n    | |  | |__  _ _ __   __ _ ___| |_ _ __ | | _____ _ __ \n    | |  | '_ \\| | '_ \\ / _` / __| | | '_ \\| |/ / _ \\ '__|\n    | |  | | | | | | | | (_| \\__ \\ | | | | |   <  __/ |   \n    |_|  |_| |_|_|_| |_|\\__, |___/_|_|_| |_|_|\\_\\___|_|   \n                         __/ |                            \n                        |___/                             ");
  this->ssid = ssid;
  this->pass = pass;
  this->authToken = authToken;
  auth_token = authToken;
  connectToWiFi(ssid, pass);
  connectToMQTT(authToken);
}

void ThingsLinker::subscribePin(const char *pin)
{
  String dynamicTopic = String(config.TOPIC) + String(authToken) + "/" + String(pin);
  client.subscribe(dynamicTopic.c_str());
  Serial.println("Subscribed to topic: " + dynamicTopic);
  myPinStrings[incrementPin] = String(pin);
  incrementPin++;
}

void ThingsLinker::connectToWiFi(const char *ssid, const char *pass)
{
  WiFi.begin(ssid, pass);

  int retries = 0;
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    retries++;
    if (retries > 10)
    {
      ESP.restart();
    }
  }

  Serial.println("WiFi Connected....IP Address:");
  Serial.println(WiFi.localIP());
}

void ThingsLinker::connectToMQTT(const char *authToken)
{
  client.setServer(config.MQTTSERVER, config.MQTTPORT);
  client.setCallback(callbackMqtt);

  while (!client.connected())
  {
    Serial.println("Connecting to MQTT...");
#if defined(ESP32)
    mqttCllent = String(authToken) + "_ESP32_" + String(config.getESP32ChipId());
#elif defined(ESP8266)
    mqttCllent = String(authToken) + "_ESP6266_" + String(config.getESP8266ChipId());
#endif
    //Serial.println(mqttCllent);
    statusTopic = String(config.TOPIC) + String(authToken) + "/status";
    if (client.connect(mqttCllent.c_str(), statusTopic.c_str(), 2, true, "OFFLINE"))
    {
      Serial.println("MQTT connected.");
      client.publish(statusTopic.c_str(), "ONLINE", true);
      client.subscribe(statusTopic.c_str());
    }
    else
    {
      Serial.print("MQTT connection failed with state ");
      Serial.println(client.state());
      delay(2000);
    }
  }

  // Important ****
  // #if defined(ESP32)
  //   xTaskCreate(taskESP32, "Task 1", 4096, NULL, tskIDLE_PRIORITY, NULL);
  // #endif
}

// Important ****
// #if defined(ESP32)
// void ThingsLinker::taskESP32(void *pvParameters)
// {
//   for (;;)
//   {
//     // Serial.println(auth_token);
//     if (!client.connected())
//     {
//       connectToMQTT(auth_token);
//     }
//     client.loop();
//   }
// }
// #endif

// Important ****
void ThingsLinker::loop()
{
  // #if defined(ESP8266)
  if (!client.connected())
  {
    connectToMQTT(auth_token);
    for (int i = 0; i < incrementPin; i++)
    {
      String secondString = myPinStrings[i];
      String dynamicTopic = String(config.TOPIC) + String(authToken) + "/" + secondString;
      client.subscribe(dynamicTopic.c_str());
      //Serial.println(secondString);
      //Serial.println("Subscribed to topic: " + dynamicTopic);
    }
  }
  client.loop();
  // #endif
}

void ThingsLinker::setCallback(std::function<void()> cb)
{
  _callback = cb;
}

void ThingsLinker::callbackMqtt(char *topic, byte *payload, unsigned int length)
{
  String message;
  message.reserve(length);
  for (int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  mqttEncodedPayload = message;
  //Serial.println(statusTopic);
  //Serial.println(topic);
  if (statusTopic == String(topic))
  {
    Serial.println("Status string: " + mqttEncodedPayload);
  }
  else
  {
    // String encoded = config.base64_encode(mqttEncodedPayload);
    //Serial.println("Encoded string: " + mqttEncodedPayload);
    String decoded = config.base64Decode(mqttEncodedPayload);
    //Serial.println("Decoded string: " + decoded);

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, decoded);
    if (error)
    {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(error.c_str());
      // return -1; // Return an error code
    }
    String newPin = doc[config.devicePin];
    String s = doc[config.deviceStatus];
    //Serial.println(s);
    int deviceStatus = doc[config.deviceStatus];
    updateMap(newPin, deviceStatus);
  }
}

void ThingsLinker::updateMap(const String &caseValue, int mapValue)
{
  // Serial.print("Doc:caseValue ");
  // Serial.println(caseValue);
  if (caseValue == "V0")
  {
    mappingStatus[0] = mapValue;
  }
  else if (caseValue == "V1")
  {
    mappingStatus[1] = mapValue;
  }
  else if (caseValue == "V2")
  {
    mappingStatus[2] = mapValue;
  }
  else if (caseValue == "V3")
  {
    mappingStatus[3] = mapValue;
  }
  else if (caseValue == "V4")
  {
    mappingStatus[4] = mapValue;
  }
  else if (caseValue == "V5")
  {
    mappingStatus[5] = mapValue;
  }
  else if (caseValue == "V6")
  {
    mappingStatus[6] = mapValue;
  }
  else if (caseValue == "V7")
  {
    mappingStatus[7] = mapValue;
  }
  else if (caseValue == "V8")
  {
    mappingStatus[8] = mapValue;
  }
  else if (caseValue == "V9")
  {
    mappingStatus[9] = mapValue;
  }
  else if (caseValue == "V10")
  {
    mappingStatus[10] = mapValue;
  }
  else if (caseValue == "V11")
  {
    mappingStatus[11] = mapValue;
  }
  else if (caseValue == "V12")
  {
    mappingStatus[12] = mapValue;
  }
  else if (caseValue == "V13")
  {
    mappingStatus[13] = mapValue;
  }
  else if (caseValue == "V14")
  {
    mappingStatus[14] = mapValue;
  }
  else if (caseValue == "V15")
  {
    mappingStatus[15] = mapValue;
  }
  else if (caseValue == "V16")
  {
    mappingStatus[16] = mapValue;
  }
  else if (caseValue == "V17")
  {
    mappingStatus[17] = mapValue;
  }
  else if (caseValue == "V18")
  {
    mappingStatus[18] = mapValue;
  }
  else if (caseValue == "V19")
  {
    mappingStatus[19] = mapValue;
  }
  else if (caseValue == "V20")
  {
    mappingStatus[20] = mapValue;
  }
  else if (caseValue == "V21")
  {
    mappingStatus[21] = mapValue;
  }
  else if (caseValue == "V22")
  {
    mappingStatus[22] = mapValue;
  }
  else if (caseValue == "V23")
  {
    mappingStatus[23] = mapValue;
  }
  else if (caseValue == "V24")
  {
    mappingStatus[24] = mapValue;
  }
  _callback();
}

int ThingsLinker::getDeviceStatus(const String &pin)
{
  for (int i = 0; i < 25; i++)
  {
    // Serial.print("Doc:mappingStatus[i] "+i);
    // Serial.println(mappingStatus[i]);
    if (pinArray[i] == pin)
    {
      deviceStatus = mappingStatus[i];
      // Serial.print("Doc: Inside loop ");
      // Serial.println(deviceStatus);
      break;
    }
  }
  // Serial.println();
  // Serial.print("Doc: outside loop ");
  // Serial.println(deviceStatus);
  return deviceStatus;
}

int ThingsLinker::getButtonAsInt(const String &pin)
{
  return getDeviceStatus(pin);
}

int ThingsLinker::getSliderAsInt(const String &pin)
{
  return getDeviceStatus(pin);
}

int ThingsLinker::getTimerAsInt(const String &pin)
{
  return getDeviceStatus(pin);
}

void ThingsLinker::setGauge(const String &pin, float sensorValue)
{
  ThingsLinker::publishMQTTMessage(pin, sensorValue);
}

void ThingsLinker::setDisplay(const String &pin, float sensorValue)
{
  ThingsLinker::publishMQTTMessage(pin, sensorValue);
}

void ThingsLinker::publishMQTTMessage(const String &pin, float sensorValue)
{
  DynamicJsonDocument doc(1024);
  doc[config.devicePin] = pin;
  doc[config.deviceStatus] = sensorValue;
  String jsonString;
  serializeJson(doc, jsonString);
  String encoded = config.base64_encode(jsonString);
  // Serial.println("Encoded string: " + mqttEncodedPayload);
  String topic = String(config.TOPIC) + String(authToken) + "/" + pin;
  client.publish(topic.c_str(), encoded.c_str(), true);
#if defined(ESP8266)
  delay(200);
#elif defined(ESP32)
  delay(200);
#endif
}

void ThingsLinker::event(const String &eventCode)
{
  DynamicJsonDocument doc(1024);
  doc[config.eventCode] = eventCode;
  String jsonString;
  serializeJson(doc, jsonString);
  String encoded = config.base64_encode(jsonString);
  Serial.println("Encoded string: " + mqttEncodedPayload);
  String topic = String(config.TOPIC) + String(authToken) + "/event";
  client.publish(topic.c_str(), encoded.c_str(), true);
#if defined(ESP8266)
  delay(200); 
#elif defined(ESP32)
  delay(200);
#endif
}