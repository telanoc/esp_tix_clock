/**************************************************************
 * WiFiManager is a library for the ESP8266/Arduino platform
 * (https://github.com/esp8266/Arduino) to enable easy 
 * configuration and reconfiguration of WiFi credentials and 
 * store them in EEPROM.
 * inspired by http://www.esp8266.com/viewtopic.php?f=29&t=2520
 * https://github.com/chriscook8/esp-arduino-apboot 
 * Built by AlexT https://github.com/tzapu
 * Licensed under MIT license
 **************************************************************/

#ifndef WiFiManager_h
#define WiFiManager_h

#include <ESP8266WiFi.h>

#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>


#define DEBUG			//until arduino ide can include defines at compile time from main sketch

#ifdef DEBUG
#define DEBUG_PRINTLN(x)  Serial.println(x)
#define DEBUG_PRINT(X)    Serial.print(X)
#else
#define DEBUG_PRINTLN(X)
#define DEBUG_PRINT(x)
#endif


class WiFiManager {
public:
  WiFiManager();

  void begin();
  void begin(char const *apName);
  void begin(char const *apName, char const *apPass);
  void begin_common();

  boolean autoConnect();
  boolean autoConnect(char const *apName);

  boolean hasConnected();

  String beginConfigMode(void);
  void startWebConfig();

  String getSSID();
  String getPassword();
  void setSSID(String s);
  void setPassword(String p);
  void resetSettings();
  //for conveniennce
  String urldecode(const char *);

  //sets timeout before webserver loop ends and exits even if there has been no setup. 
  //usefully for devices that failed to connect at some point and got stuck in a webserver loop
  //in seconds
  void setTimeout(unsigned long seconds);

  int serverLoop();

private:
  const int WM_DONE = 0;
  const int WM_WAIT = 10;

  const String HTTP_404 = 
		"HTTP/1.1 404 Not Found\r\n\r\n";
  
  const String HTTP_200 =
		"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
  
  const String HTTP_HEAD =
		"<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/><title>{v}</title><link rel='icon' type='image/png' href='data:image/png;base64,iVBORw0KGgo='>";
  
  const String HTTP_STYLE =
		"<style>div,input {margin-bottom: 5px;}body{width:200px;display:block;margin-left:auto;margin-right:auto;}</style>";
  
  const String HTTP_SCRIPT =
		"<script>function c(l){document.getElementById('s').value=l.innerText||l.textContent;document.getElementById('p').focus();}</script>";
  
  const String HTTP_HEAD_END = 
		"</head><body>";
  
  const String HTTP_ITEM =
		"<div><a href='#' onclick='c(this)'>{v}</a> {v2}</div>";
  
  const String HTTP_FORM =
		"<form method='get' action='wifisave'><input id='s' name='s' length=32 placeholder='SSID'><input id='p' name='p' length=64 placeholder='password'><br/><input type='submit'></form>";
  
  const String HTTP_SAVED =
		"<div>Credentials Saved<br />Node will reboot in 5 seconds.</div>";

  const String HTTP_END = 
		"</body></html>";

  int _eepromStart;
  const char *_apName = "no-net";
  const char *_apPass = "";
  String _ssid = "";
  String _pass = "";
  unsigned long timeout = 0;
  unsigned long start = 0;


  String getEEPROMString(int start, int len);
  void setEEPROMString(int start, int len, String string);

  bool keepLooping = true;
  int status = WL_IDLE_STATUS;
  void connectWifi(String ssid, String pass);

  void sendStdHeaderBits();
  void handleRoot();
  void handleWifi();
  void handleWifiSave();
  void handleNotFound();
  void handle204();
  boolean captivePortal();

  // DNS server
  const byte DNS_PORT = 53;

  boolean isIp(String str);
  String toStringIp(IPAddress ip);

  boolean connect;

};



#endif
