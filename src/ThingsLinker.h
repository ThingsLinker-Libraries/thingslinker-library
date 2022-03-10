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
class ThingsLinker
{
private:
  const char *ssid;
  const char *pass;
  String authToken;
  String pin;
  String timerStatus;
  float sensorValue;

public:
  ThingsLinker(const char *ssid, const char *pass);
  ThingsLinker();
  ThingsLinker(String pin);
  void setWifi();
  void run(String authToken);
  int getOnOff(String pin);
  int getTimerOnOff(String pin);
  int getSlider(String pin);
  void putTimer(String pin,String timerStatus);
  void setGauge(String pin, float sensorValue);
};

#endif