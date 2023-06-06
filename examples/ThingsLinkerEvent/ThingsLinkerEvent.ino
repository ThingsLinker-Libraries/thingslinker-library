/*************************************************************
  ThingsLinker is a platform that provides an Android app and a website for 
  controlling Esp8266, Esp32, and similar devices over the Internet. With ThingsLinker, 
  you can effortlessly build graphic interfaces for all your projects by simply 
  adding widgets.

  For downloads, documentation, and tutorials, please visit: https://thingslinker.com
  ThingsLinker community: https://community.thingslinker.com
  Follow us: https://www.fb.com/ThingsLinker
             https://twitter.com/ThingsLinker
             https://www.instagram.com/thingslinker
             https://www.linkedin.com/company/thingslinker
  
  ThingsLinker library is licensed under MIT license
  This example code is in the public domain.

 *************************************************************
  Project setup by ThingsLinker:
 *************************************************************/

#include <ThingsLinker.h> // include ThingsLinker library

ThingsLinker thingsLinker; // create an instance of ThingsLinker class

const char *authToken = "YourAuthToken"; // set authentication token

const char *ssid = "YourWifiName";         // set WiFi network name
const char *password = "YourWifiPassword"; // set WiFi network password

#ifdef ESP8266
const int LED_PIN = 2; // set LED pin for ESP8266
#else
const int LED_PIN = 2; // set LED pin for ESP32
#endif

void setup()
{
  Serial.begin(115200); // initialize serial communication

  thingsLinker.begin(ssid, password, authToken); // begin ThingsLinker communication

  pinMode(LED_PIN, OUTPUT); // set LED pin as output

  thingsLinker.subscribePin("V0"); // subscribe to V0 pin in ThingsLinker

  thingsLinker.setCallback(callback); // set callback function to be called when V0 pin changes
}

void callback()
{
  int buttonStatus = thingsLinker.getButtonAsInt("V0"); // get the button status from V0 pin

  Serial.print("Get Button Status: ");
  Serial.println(buttonStatus);

  if (buttonStatus == 0) // if button is not pressed
  {
    digitalWrite(LED_PIN, LOW); // turn off LED
  }
  else if (buttonStatus == 1) // if button is pressed
  {
    digitalWrite(LED_PIN, HIGH); // turn on LED
    thingsLinker.event("YourEventKey"); // Copy 'Event Key' from ThingsLinker app
  }
}

void loop()
{
  thingsLinker.loop(); // check for changes in ThingsLinker pins
}
