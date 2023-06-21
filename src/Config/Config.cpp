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

const char *Config::MQTTSERVER = "181.215.79.89";

const int Config::MQTTPORT = 1883;

const char *Config::TOPIC = "thingslinker/";

const char *Config::devicePin = "devicePin";

const char *Config::deviceStatus = "deviceStatus";

const char *Config::eventCode = "eventCode";

const char *Config::event = "/event";

const char *Config::charts = "charts/";

const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";



#if defined(ESP32)
String Config::getESP32ChipId()
{
    uint64_t chipid;
    chipid = ESP.getEfuseMac();
    char upperStr[5];                                          // 4 characters for the value, plus a null terminator
    sprintf(upperStr, "%04X", (uint16_t)(chipid >> 32));       // format the upper 2 bytes of chipid as a hexadecimal string with 4 digits and store it in upperStr
    char lowerStr[9];                                          // 8 characters for the value, plus a null terminator
    sprintf(lowerStr, "%08X", (uint32_t)chipid);               // format the lower 4 bytes of chipid as a hexadecimal string with 8 digits and store it in lowerStr
    String chipidString = String(upperStr) + String(lowerStr); // concatenate the upper and lower strings to form the full chip ID
    //Serial.println("ESP32 Chip ID: " + chipidString);
    return chipidString;
}
#endif
#if defined(ESP8266)
String Config::getESP8266ChipId()
{
    uint32_t chipid;
    chipid = ESP.getChipId();
    char chipidStr[9];                  // 8 characters for the value, plus a null terminator
    sprintf(chipidStr, "%08X", chipid); // format chipid as a hexadecimal string with 8 digits and store it in chipidStr
    String chipidString(chipidStr);     // create a String object from the char array
    //Serial.println("ESP8266 Chip ID: " + chipidString);
    return chipidString;
}
#endif


String Config::base64_encode(String str) {
  String encoded = "";
  int i = 0;
  int j = 0;
  int char_array_3[3];
  int char_array_4[4];
  int in_len = str.length();
  char const *input = str.c_str();

  while (in_len--) {
    char_array_3[i++] = *(input++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++)
        encoded += base64Chars[char_array_4[i]];

      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; j < i + 1; j++)
      encoded += base64Chars[char_array_4[j]];

    while ((i++ < 3))
      encoded += "";
  }

  return encoded;
}


String Config::base64Decode(String base64Encoded) {
  String decoded = "";
  const char* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int inputLength = base64Encoded.length();

  // Calculate the output length and allocate memory
  int outputLength = (inputLength * 3) / 4;
  if (base64Encoded[inputLength - 1] == '=') {
    outputLength--;
  }
  if (base64Encoded[inputLength - 2] == '=') {
    outputLength--;
  }
  char* output = new char[outputLength + 1];

  // Decode the input string in blocks of 4 characters
  for (int i = 0, j = 0; i < inputLength; i += 4, j += 3) {
    uint32_t block = 0;

    // Decode the 4 characters in the input block
    for (int k = 0; k < 4; k++) {
      char c = base64Encoded.charAt(i + k);
      const char* indexPtr = strchr(base64Chars, c);
      if (indexPtr != NULL) {
        int index = indexPtr - base64Chars;
        block = (block << 6) | index;
      }
    }

    // Extract the 3 bytes from the decoded block
    output[j] = (block >> 16) & 0xFF;
    if (j + 1 < outputLength) {
      output[j + 1] = (block >> 8) & 0xFF;
    }
    if (j + 2 < outputLength) {
      output[j + 2] = block & 0xFF;
    }
  }

  // Null-terminate the output string
  output[outputLength] = '\0';

  // Convert the output buffer to a String object
  decoded = String(output);

  // Free the allocated memory
  delete[] output;

  return decoded;
}


int Config::countPadding(String base64Encoded) {
  int paddingCount = 0;
  int inputLength = base64Encoded.length();
  
  if (inputLength > 0 && base64Encoded.charAt(inputLength - 1) == '=') {
    paddingCount++;
  }
  if (inputLength > 1 && base64Encoded.charAt(inputLength - 2) == '=') {
    paddingCount++;
  }
  
  return paddingCount;
}

