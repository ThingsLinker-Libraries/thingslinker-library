# ThingsLinker Arduino Library

<h3>ThingsLinker was designed for the Internet of Things. It can control hardware remotely, it can display sensor data, it can store data, visualize it, and do many other cool things.</h3>

 <ul><li>Connect Esp8266 and Esp32 device using ThingsLinker platform. You can control devices using Android Mobile Phone , Website and Linux Desktop.</li></ul>

# <h3>How to use ThingsLinker library</h3>
 <p>&emsp; Step 1 - Download the ThingsLinker library</p>
 <p>&emsp; Step 2 - Import this library to Arduino IDE. Guide <a href="http://arduino.cc/en/guide/libraries" rel="nofollow">here</a></p>
 <p>&emsp; Step 3 - In Arduino IDE, select File -> Examples -> ThingsLinker -> ThingsLinkerOnOff</p>
 <p>&emsp; Step 4 - Update Auth Token in the sketch and upload it to Arduino</p>
 <p></p>

# Usage : 

- <h3>Include in your sketch</h3>

```cpp
#include <ThingsLinker.h>
```
- Initialize library, in your setup function add, NOTEif you are using non blocking you will make sure you create this in global scope or handle appropriatly , it will not work if in setup and using non blocking mode.

```cpp
ThingsLinker thingsLinker;
```

- Setup wifi connect using ThingsLinker constructor.

```cpp
void setup()
{
 //first parameter is name of access point, second is the password
 ThingsLinker(ssid, password); 
}
```

- Setup project auth token

```cpp
void loop()
{
  thingsLinker.run(authToken);
}
```
# Widgets 
- <h3>Setup Button Widget and Get Value</h3>

```cpp
void loop()
{
  //You can pass Virtual pin. ex: V0 to V24, Get value
  //  You get status 0 or  1
  // You can setup your logic inside the conditions

  if (thingsLinker.getOnOff("V0") == 0) 
   {
     digitalWrite(LED, LOW);
   }
   else if (thingsLinker.getOnOff("V0") == 1)
   {
     digitalWrite(LED, HIGH);
   }

   thingsLinker.getOnOff("V0”); 
}
```

- <h3>Setup Gauge Widget</h3>

```cpp
void loop()
{
  // 'setGauge' has two parameters 
  // 1 - You can pass Virtual pin. ex: V0 to V24
  // 2 - Put Sensor value as a floating format

 thingsLinker.setGauge("V0”, ”Put sensor value”);
}
```

- <h3>Setup Slider Widget and Get Value</h3>

```cpp
void loop()
{
  // 'getSlider' has one parameters 
  // 1 - You can pass Virtual pin. ex: V0 to V24
  // 2 - Get Slider value 0 to 100

  thingsLinker.getSlider("V0");  
}
```

- <h3>Setup Timer Widget and Get Value</h3>

```cpp
void loop()
{
 //You can pass Virtual pin. ex: V0 to V24, Get value
 //  You get status 0 or  1
 // You can setup your logic inside the conditions

 if (thingsLinker.getTimerOnOff("V0") == 0) 
  {
    digitalWrite(LED, LOW);
  }
  else if (thingsLinker.getTimerOnOff("V0") == 1)
  {
    digitalWrite(LED, HIGH);
  }

  thingsLinker.getTimerOnOff("V0”); 
}
```

 
# ThingsLinker Documentation

  <ul><li>ThingsLinker <a href="https://blog.thingslinker.com/" rel="nofollow">Official Website</a></li></ul>
