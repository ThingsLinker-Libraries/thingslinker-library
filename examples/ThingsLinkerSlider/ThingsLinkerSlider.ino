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

// This line imports the ThingsLinker library.
#include <ThingsLinker.h>

// Create an instance of the ThingsLinker class
ThingsLinker thingsLinker;

// Authentication token for ThingsLinker platform
const char *authToken = "YourAuthToken";

// Wi-Fi network's SSID and password
const char *ssid = "YourWifiName";
const char *password = "YourWifiPassword";

// Define pins for servo based on the board being used ESP8266
const int SERVO_PIN = 4;

void setup()
{
  // Initialize serial communication
  Serial.begin(115200);

  // Initialize ThingsLinker class with Wi-Fi credentials and authentication token
  thingsLinker.begin(ssid, password, authToken);

  // Attach servo to pin
  myservo.attach(SERVO_PIN);

  // Subscribe to pins "V0" on the ThingsLinker platform
  thingsLinker.subscribePin("V0");

  // Set callback function to execute when data is received from subscribed pins
  thingsLinker.setCallback(callback);
}

// Define the callback function to execute when data is received from subscribed pins
void callback()
{
  /* This line of code obtains the current value of a slider with the ID "V1" using
  the ThingsLinker library and assigns the result to the integer variable "sliderValue".*/
  int sliderValue = thingsLinker.getSliderAsInt("V0");
  Serial.print("Get getSliderAsInt: "); // Print message to serial monitor
  Serial.println(sliderValue);           // Read data from pin "V1" and print it to the serial monitor
  myservo.write((sliderValue * 360) / 100); // Set servo angle based on value of pin "V1"
}

void loop()
{
  thingsLinker.loop(); // Check for incoming data from subscribed pins
}
