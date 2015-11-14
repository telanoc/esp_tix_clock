# WiFiManager
ESP8266 WiFi Connection manager with save to EEPROM and web config portal

Captive portal should also work, I have tested it only on iOS.

First attempt at a library. Lots more changes and fixes to do. Adding examples
also needs doing.

This works with the ESP8266 Arduino platform https://github.com/esp8266/Arduino

> v0.1 works with the staging release ver. 1.6.5-1044-g170995a, built on Aug
> 10, 2015 of the ESP8266 Arduino library.  latest comit should work with the
> latest staging version

####Quick Start
- Checkout library to your Arduino libraries folder

- Include in your sketch

```Arduino
#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
```

- Initialise library, the paramater is the Acces Point Name
```
//parameter is eeprom start
WiFiManager wifi;
```

- In your setup function add
```
//parameter is name of access point
wifi.autoConnect("AP-NAME");
```

After you write your sketch and start the ESP, it will try to connect to WiFi.
If it fails it starts in Access Point mode.  While in AP mode, connect to it
then open a browser to the gateway IP, default 192.168.4.1, configure wifi,
save and it should reboot and connect.

Also see examples.


####Inspiration
- http://www.esp8266.com/viewtopic.php?f=29&t=2520

To be continued...

Changes by Pete Cervasio for inclusion into the esp_tix_clock project:

a). Tired of seeing stuff about favicon, the header was modified to add:
    <link rel='icon' type='image/png' href='data:image/png;base64,iVBORw0KGgo='>

b). HTTP_ITEM was modified so the encryption type could be displayed for
    each found AP.

c). Moved standard looking 'Cach-Control' and 'Expires' data into its own
    function so it's only defined once.

d). Removed a lot of commented code that was confusing to me.

e). I think I ran this through indent, too.
