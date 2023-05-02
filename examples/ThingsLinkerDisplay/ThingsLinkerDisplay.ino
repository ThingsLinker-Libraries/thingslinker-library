/*************************************************************
  ThingsLinker is a platform with Android app, Ubuntu software and Website to control
  Esp8266 and Esp32 and the likes over the Internet.
  You can easily build graphic interfaces for all your
  projects by simply dragging and dropping widgets.

  Downloads, docs, tutorials: https://blog.thingslinker.com/

  This example code is in public domain.

 *************************************************************
  Project setup by ThingsLinker:
 *************************************************************/



#include <ThingsLinker.h>  // Include ThingsLinker library

ThingsLinker thingsLinker;  // Create an instance of the ThingsLinker class

const char *authToken = "YourAuthToken";  // Authentication token for ThingsLinker platform
const char *ssid = "YourWifiName";  // Wi-Fi network's SSID
const char *password = "YourWifiPassword";  // Wi-Fi network's password

void setup()
{
  Serial.begin(115200);  // Initialize serial communication with baud rate 115200
  thingsLinker.begin(ssid, password, authToken);  // Initialize ThingsLinker class with Wi-Fi credentials and authentication token
}

void loop()
{
  thingsLinker.loop();  // Check for incoming data from subscribed pins

  float y = random(1, 401) / 100.0;  // Generate a random float between 1 and 4, with two decimal places
  Serial.print("Value: ");  // Print message to serial monitor
  Serial.println(y);  // Print generated value to serial monitor

  thingsLinker.setDisplay("V3", y);  // Send generated value to pin "V3" on ThingsLinker platform for display
}
