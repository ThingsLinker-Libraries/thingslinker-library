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

#include <ThingsLinker.h>

ThingsLinker thingsLinker;

const char *authToken = "YourAuthToken"; //You can get auth token in the ThingsLinker App.

const char *ssid = "YourWifiName";
const char *password = "YourWifiPassword";

int LED = D1;

void setup()
{
  Serial.begin(9600);
  ThingsLinker(ssid, password); // Setup wifi connect using ThingsLinker constructor.
  pinMode(LED, OUTPUT);
}

void loop()
{
  thingsLinker.run(authToken);         // Setup project auth token

  float y = random(1, 401) / 100.0;
  Serial.print("Value: ");
  Serial.println(y);

  // 'setGauge' has two parameters 
  // 1 - You can pass Virtual pin. ex: V0 to V24
  // 2 - Put Sensor value as a floating format
  thingsLinker.setGauge("V0",y);       
  delay(3000);
}