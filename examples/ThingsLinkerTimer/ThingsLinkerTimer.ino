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


// This line imports the ThingsLinker library.
#include <ThingsLinker.h>

// This line creates a new instance of the ThingsLinker class.
ThingsLinker thingsLinker;

// This line defines the authentication token for accessing the ThingsLinker API.
const char *authToken = "YourAuthToken";

// These lines define the name and password of the Wi-Fi network that the device will connect to.
const char *ssid = "YourWifiName";
const char *password = "YourWifiPassword";

// These lines set the LED pin based on the type of board being used.
#ifdef ESP8266
const int LED_PIN = 2; // set LED pin for ESP8266
#else
const int LED_PIN = 2; // set LED pin for ESP32
#endif

// This function runs once at the start of the program.
void setup()
{
  // This line initializes the serial communication.
  Serial.begin(115200);

  // This line starts the ThingsLinker library with the specified Wi-Fi credentials and authentication token.
  thingsLinker.begin(ssid, password, authToken);

  // This line sets the LED pin as an output.
  pinMode(LED_PIN, OUTPUT);

  // This line subscribes to updates for pin "V0".
  thingsLinker.subscribePin("V0");

  // This line sets the callback function to be executed when an update is received.
  thingsLinker.setCallback(callback);
}

// This function is called when an update is received from the ThingsLinker API.
void callback()
{
  // This line retrieves the timer status of pin "V0" using the ThingsLinker library.
  int timerStatus = thingsLinker.getTimerAsInt("V0");

  // This line prints the timer status to the serial monitor.
  Serial.print("Get Button Status: ");
  Serial.println(timerStatus);

  // These lines toggle the LED pin based on the timer status.
  if (timerStatus == 0)
  {
    digitalWrite(LED_PIN, LOW); // turn off LED
  }
  else if (timerStatus == 1)
  {
    digitalWrite(LED_PIN, HIGH); // turn on LED
  }
}

// This function runs continuously in a loop.
void loop()
{
  // This line runs the ThingsLinker library to check for any updates or changes.
  thingsLinker.loop();
}
