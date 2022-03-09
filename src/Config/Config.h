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
class Config
{
private:

public:
    Config();
    static String BASH_URL;
    static String ENDPOINT;
    static String DEVICEENDPOINT;
    static const char *CONTENTTYPE;
    static const char *CONTENTTYPEVALUE;
    static const char *devicePin;
    static const char *deviceType;
    static String deviceStatus;
    static const char *body;
    static const char *currentTime;
    static const char *startTime;
    static const char *stopTime;
    static const char *_id;
};

#endif