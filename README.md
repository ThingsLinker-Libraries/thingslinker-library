

```cpp
  _______ _     _                 _ _       _             
 |__   __| |   (_)               | (_)     | |            
    | |  | |__  _ _ __   __ _ ___| |_ _ __ | | _____ _ __ 
    | |  | '_ \| | '_ \ / _` / __| | | '_ \| |/ / _ \ '__|
    | |  | | | | | | | | (_| \__ \ | | | | |   <  __/ |   
    |_|  |_| |_|_|_| |_|\__, |___/_|_|_| |_|_|\_\___|_|   
                         __/ |                            
                        |___/                             
```

# ThingsLinker Arduino Library

<h3>Now, the ThingsLinker library has added support for the MQTT protocol. The library uses the PubSubClient library, which can be found at <a href="http://arduino.cc/en/guide/libraries" rel="nofollow">http://arduino.cc/en/guide/libraries</a>, to enable MQTT functionality.</h3>
<br>
<h3>ThingsLinker was specifically designed for the Internet of Things (IoT), with a wide range of capabilities. It allows for remote control of hardware, display of sensor data, data storage, visualization, and much more. With its versatile features, ThingsLinker provides a comprehensive solution for managing IoT devices and data.</h3>
<br>
 <ul><li>With ThingsLinker, you can easily connect and control both ESP8266 and ESP32 devices. Whether you use the iOS or Android application or the website, ThingsLinker enables seamless remote control of your devices.</li></ul>

# Installation 

<h3>To use the ThingsLinker library, follow these steps:</h3>
<br>
 <p>&emsp; 1. Download the ThingsLinker library</p>
 <p>&emsp; 2. Import the library to Arduino IDE (check out the guide <a href="http://arduino.cc/en/guide/libraries" rel="nofollow">here</a>)</p>
 <p>&emsp; 3. In Arduino IDE, select File -> Examples -> ThingsLinker -> ThingsLinkerButton</p>
 <p>&emsp; 4. Update your auth token in the sketch and upload it to your Arduino device</p>
<br>

# Usage

- <h3>To use the ThingsLinker library in your sketch, include the following line at the top:</h3>

```cpp
#include <ThingsLinker.h>
```

- This statement creates an instance of the ThingsLinker library called "thingsLinker." This instance can be used to access the various functions and features provided by ThingsLinker.

```cpp
ThingsLinker thingsLinker;
```

- This statement calls the "begin" function of the thingsLinker instance, which initializes the library with the provided SSID, password, and authentication token parameters. This allows the device to connect to the network and authenticate with the ThingsLinker platform to enable IoT functionality.

```cpp
void setup()
{
 thingsLinker.begin(ssid, password, authToken);
}
```

- This statement calls the "subscribePin" function of the thingsLinker instance, which subscribes to changes in the virtual pin "V0". This enables the device to receive updates when the value of V0 changes and allows for remote control of the pin through the ThingsLinker platform.

```cpp
void setup()
{
  thingsLinker.subscribePin("V0");
}
```

- This statement calls the "setCallback" function of the thingsLinker instance, which sets the callback function to be executed when a message is received on any subscribed virtual pin. The callback function can be defined by the user and can be used to process incoming data or execute specific actions based on the received message.

```cpp
void setup()
{
  thingsLinker.setCallback(callback);
}
```

- This is an example of a callback function that can be used with ThingsLinker. In this example, the "getButtonAsInt" function is called to retrieve the current value of virtual pin "V0" and store it in the "buttonStatus" variable. The variable is then printed to the serial monitor. The user can add their own logic to the function to process the retrieved data or execute specific actions based on the received message.

```cpp
void callback()
{
  int buttonStatus = thingsLinker.getButtonAsInt("V0"); 

  Serial.print("Get Button Status: "); 
  Serial.println(buttonStatus);

  // Your code here
}
```

- This statement calls the "loop" function of the thingsLinker instance, which allows the library to continuously listen for incoming messages and execute the associated callback function when a message is received. This function should be called in the device's main loop to enable ThingsLinker to function correctly.

```cpp
void loop()
{
  thingsLinker.loop();
}
```
- This example shows how to use the `setDisplay` function to publish data on the ThingsLinker platform.

```cpp
void loop()
{
  float y = random(1, 401) / 100.0;  /
  Serial.print("Value: ");  
  Serial.println(y);  

  thingsLinker.setDisplay("V3", y); 
}
```

# ThingsLinker Widgets Examples

<h3>ThingsLinker offers various widgets that you can use to display or publish data. Here are some examples:</h3>
<br>

<h2>Get Button Value Example</h2>
<br>
<p>This example shows how to retrieve an integer value from the ThingsLinker platform by calling the `getButtonAsInt` method of the `thingsLinker` object.</p>

```cpp
void callback()
{
  int buttonStatus = thingsLinker.getButtonAsInt("V0"); 

  Serial.print("Get Button Status: "); 
  Serial.println(buttonStatus);

  if (buttonStatus == 0) 
  {
    digitalWrite(LED_PIN, LOW); 
  }
  else if (buttonStatus == 1) 
  {
    digitalWrite(LED_PIN, HIGH); 
  }
}
```

<h2>Set Display Example</h2>
<br>
<p>This example shows how to use the `setDisplay` function to publish data on the ThingsLinker platform.</p>

```cpp
void loop()
{
  float y = random(1, 401) / 100.0;  /
  Serial.print("Value: ");  
  Serial.println(y);  

  thingsLinker.setDisplay("V3", y); 
}
```

<h2>Set Gauge Example</h2>
<br>
<p>This example shows how to use the `setGauge` function to publish data on the ThingsLinker platform for gauge display.</p>

```cpp
void loop()
{
  float y = random(1, 401) / 100.0;  
  Serial.print("Value: ");  
  Serial.println(y); 

  thingsLinker.setGauge("V3", y);  platform for gauge display
  delay(1000); 
}
```

<h2>Get Slider Value Example</h2>
<br>
<p>This example shows how to retrieve an integer value from the ThingsLinker platform by calling the `getSliderAsInt` method of the `thingsLinker` object.</p>

```cpp
void loop()
{
  int sliderValue = thingsLinker.getSliderAsInt("V0");
  Serial.print("Get getSliderAsInt: "); 
  Serial.println(sliderValue);         
}
```

<h2>Get Timer Value Example</h2>
<br>
<p>This example shows how to retrieve an integer value from the ThingsLinker platform by calling the `getTimerAsInt` method of the `thingsLinker` object.</p>

```cpp
void callback()
{
  int timerStatus = thingsLinker.getTimerAsInt("V0");
  Serial.print("Get Button Status: ");
  Serial.println(timerStatus);
  if (timerStatus == 0)
  {
    digitalWrite(LED_PIN, LOW); 
  }
  else if (timerStatus == 1)
  {
    digitalWrite(LED_PIN, HIGH);
  }
}
```

 
# ThingsLinker Documentation

  <ul><li>ThingsLinker <a href="https://blog.thingslinker.com/" rel="nofollow">Official Website</a></li></ul>
