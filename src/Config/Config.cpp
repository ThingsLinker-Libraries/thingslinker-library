/**
 * @file       Config.cpp
 * @author     Chetan Mahajan
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2021 Chetan Mahajan
 * @date       April 2021
 * @brief
 *
 */

#include "Config.h"

Config::Config()
{
}

String Config::BASH_URL = "https://server.thingslinker.com/"; 

String Config::ENDPOINT = 
"devicesESP/";

String Config::DEVICEENDPOINT = 
"device/";

const char *Config::CONTENTTYPE = 
"Content-Type";

const char *Config::CONTENTTYPEVALUE = 
"application/x-www-form-urlencoded";

const char *Config::devicePin = 
"devicePin";

const char *Config::deviceType = 
"deviceType";

String Config::deviceStatus = 
"deviceStatus";

const char *Config::body = 
"body";

const char *Config::currentTime = 
"currentTime";

const char *Config::startTime = 
"startTime";

const char *Config::stopTime = 
"stopTime";

const char *Config::_id = 
"_id";