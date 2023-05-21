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
  thingsLinker.run(authToken);  // Check for incoming data from subscribed pins and perform other background tasks

  float y = random(1, 401) / 100.0;  // Generate a random float between 1 and 4, with two decimal places
  Serial.print("Value: ");  // Print message to serial monitor
  Serial.println(y);  // Print generated value to serial monitor

  thingsLinker.setGauge("V3", y);  // Send generated value to pin "V3" on ThingsLinker platform for gauge display
  delay(1000);  // Wait for 1 second before generating the next value
}
