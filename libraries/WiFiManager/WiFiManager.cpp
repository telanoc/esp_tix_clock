/**************************************************************
 * WiFiManager is a library for the ESP8266/Arduino platform
 * (https://github.com/esp8266/Arduino) to enable easy
 * configuration and reconfiguration of WiFi credentials and
 * store them in EEPROM.
 * inspired by: 
 * http://www.esp8266.com/viewtopic.php?f=29&t=2520
 * https://github.com/chriscook8/esp-arduino-apboot
 * https://github.com/esp8266/Arduino/tree/esp8266/hardware/esp8266com/esp8266/libraries/DNSServer/examples/CaptivePortalAdvanced
 * Built by AlexT https://github.com/tzapu
 * Licensed under MIT license
 **************************************************************/

#include "WiFiManager.h"

DNSServer dnsServer;

// Web server
ESP8266WebServer server(80);

WiFiManager::WiFiManager()
{
}

void
 WiFiManager::begin()
{
    begin("NoNetESP");
}

void WiFiManager::begin(char const *apName, char const *apPass)
{
    _apPass = apPass;
    begin(apName);
}

void WiFiManager::begin(char const *apName)
{
    DEBUG_PRINTLN("");
    _apName = apName;
    start = millis();

    DEBUG_PRINT("Configuring access point... ");
    DEBUG_PRINTLN(_apName);
    if (_apPass[0] == 0) {
        WiFi.softAP(_apName);
    } else {
        WiFi.softAP(_apName, _apPass);
    }

    delay(500); // Without delay I've seen the IP address blank
    DEBUG_PRINT("AP IP address: ");
    DEBUG_PRINTLN(WiFi.softAPIP());

    /*
     * Setup the DNS server redirecting all the domains to the apIP 
     */
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    /*
     * Setup web pages: root, wifi config pages, SO captive portal
     * detectors and not found. 
     */
    server.on("/", std::bind(&WiFiManager::handleRoot, this));
    server.on("/wifi", std::bind(&WiFiManager::handleWifi, this));
    server.on("/wifisave", std::bind(&WiFiManager::handleWifiSave, this));

    /*
     * Android/Chrome OS captive portal check.
     */
    server.on("/generate_204", std::bind(&WiFiManager::handle204, this));
    
    /*
     * Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
     */
    server.on("/fwlink", std::bind(&WiFiManager::handleRoot, this));
    
    server.onNotFound(std::bind(&WiFiManager::handleNotFound, this));
    
    /*
     * Web server start
     */
    server.begin();
    DEBUG_PRINTLN("HTTP server started");
}

boolean WiFiManager::autoConnect()
{
    return autoConnect("NoNetESP");
}

boolean WiFiManager::autoConnect(char const *apName)
{
    DEBUG_PRINTLN("AutoConnect");
    // read eeprom for ssid and pass
    String ssid = getSSID();
    String pass = getPassword();

    WiFi.mode(WIFI_STA);
    connectWifi(ssid, pass);
    int s = WiFi.status();
    if (s == WL_CONNECTED) {
        // connected
        return true;
    }
    // delay(1000);
    // not connected
    // setup AP
    WiFi.mode(WIFI_AP);

    connect = false;
    begin(apName);

    while (1) {
        // DNS
        dnsServer.processNextRequest();
        // HTTP
        server.handleClient();

        if (connect) {
            delay(5000);
            ESP.reset();
            delay(1000);
        }

        yield();
    }

    return false;
}


void WiFiManager::connectWifi(String ssid, String pass)
{
    DEBUG_PRINTLN("Connecting as wifi client...");
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
    int connRes = WiFi.waitForConnectResult();
    DEBUG_PRINT("connRes: ");
    DEBUG_PRINTLN(connRes);
}


String WiFiManager::getSSID()
{
    if (_ssid == "") {
        DEBUG_PRINT("Reading EEPROM SSID: ");
        _ssid = getEEPROMString(0, 32);
        DEBUG_PRINTLN(_ssid);
    }
    return _ssid;
}


String WiFiManager::getPassword()
{
    if (_pass == "") {
        DEBUG_PRINT("Reading EEPROM Password: ");
        _pass = getEEPROMString(32, 64);
        DEBUG_PRINTLN(_pass);
    }
    return _pass;
}


void WiFiManager::setSSID(String s)
{
    DEBUG_PRINT("Save SSID: ");
    DEBUG_PRINTLN(s);
    _ssid = s;
    setEEPROMString(0, 32, _ssid);
}


void WiFiManager::setPassword(String p)
{
    DEBUG_PRINT("Save password: ");
    DEBUG_PRINTLN(p);
    _pass = p;
    setEEPROMString(32, 64, _pass);
}


String WiFiManager::getEEPROMString(int start, int len)
{
    EEPROM.begin(512);
    delay(10);
    String string = "";
    for (int i = _eepromStart + start; i < _eepromStart + start + len; i++) {
        // DEBUG_PRINT(i);
        string += char (EEPROM.read(i));
    }
    EEPROM.end();
    return string;
}


void WiFiManager::setEEPROMString(int start, int len, String string)
{
    EEPROM.begin(512);
    delay(10);
    int si = 0;
    for (int i = _eepromStart + start; i < _eepromStart + start + len; i++) {
        char c;
        if (si < string.length()) {
            c = string[si];
        } else {
            c = 0;
        }
        EEPROM.write(i, c);
        si++;
    }
    EEPROM.end();
}



int WiFiManager::serverLoop()
{

}

String WiFiManager::urldecode(const char *src)
{
    String decoded = "";
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2]))
            && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';

            decoded += char (16 * a + b);
            src += 3;
        } else if (*src == '+') {
            decoded += ' ';
            *src++;
        } else {
            decoded += *src;
            *src++;
        }
    }
    decoded += '\0';

    return decoded;
}

void WiFiManager::resetSettings()
{
    // need to call it only after lib has been started with autoConnect or 
    // begin
    setEEPROMString(0, 32, "-");
    setEEPROMString(32, 64, "-");

    DEBUG_PRINTLN("WiFi settings invalidated");
    delay(200);
    WiFi.disconnect();
}

void WiFiManager::setTimeout(unsigned long seconds)
{
    timeout = seconds * 1000;
}

/*
 * This seems to be a standard thing that gets sent out.  Making it live
 * in its own function so that it can be in one spot 
 */
void WiFiManager::sendStdHeaderBits()
{
    server.sendHeader("Cache-Control",
                      "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
}

/** Handle root or redirect to captive portal */
void WiFiManager::handleRoot()
{
    DEBUG_PRINT("Handle root");
    if (captivePortal()) {      // If captive portal redirect instead of
        // displaying the page.
        return;
    }

    sendStdHeaderBits();
    server.send(200, "text/html", "");  
    // Empty content inhibits Content-length header so we have to close the
    // socket ourselves.

    String head = HTTP_HEAD;
    head.replace("{v}", "Options");
    server.sendContent(head);
    server.sendContent(HTTP_SCRIPT);
    server.sendContent(HTTP_STYLE);
    server.sendContent(HTTP_HEAD_END);

    server.
        sendContent
        ("<form action=\"/wifi\" method=\"get\"><button>Configure WiFi</button></form>");

    server.sendContent(HTTP_END);

    // Stop is needed because we sent no content length
    server.client().stop();
}


/** Wifi config page handler */
void WiFiManager::handleWifi()
{
    sendStdHeaderBits();
    server.send(200, "text/html", "");  // Empty content inhibits
    // Content-length header so we
    // have to close the socket
    // ourselves.


    String head = HTTP_HEAD;
    head.replace("{v}", "Config ESP");
    server.sendContent(head);
    server.sendContent(HTTP_SCRIPT);
    server.sendContent(HTTP_STYLE);
    server.sendContent(HTTP_HEAD_END);

    int n = WiFi.scanNetworks();
    DEBUG_PRINTLN("AP scan done");
    if (n == 0) {
        DEBUG_PRINTLN("no networks found");
        server.sendContent("<div>No networks found. Refresh to scan again.</div>");
    } else {
        for (int i = 0; i < n; ++i) {
            DEBUG_PRINT(WiFi.SSID(i));
            DEBUG_PRINT("  ");
            DEBUG_PRINTLN(WiFi.RSSI(i));
            String item = HTTP_ITEM;
            item.replace("{v}", WiFi.SSID(i));

            switch (WiFi.encryptionType(i)) {
            case ENC_TYPE_WEP:
                item.replace("{v2}", "- WEP");
                break;
            case ENC_TYPE_TKIP:
                item.replace("{v2}", "- WPA");
                break;
            case ENC_TYPE_CCMP:
                item.replace("{v2}", "- WPA2");
                break;
            case ENC_TYPE_NONE:
                item.replace("{v2}", "- None");
                break;
            case ENC_TYPE_AUTO:
                item.replace("{v2}", "- Auto");
                break;
            }

            server.sendContent(item);
            yield();
        }
    }

    server.sendContent(HTTP_FORM);
    server.sendContent(HTTP_END);
    server.client().stop();

    DEBUG_PRINTLN("Sent config page");
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void WiFiManager::handleWifiSave()
{
    DEBUG_PRINTLN("wifi save");
    // server.arg("s").toCharArray(ssid, sizeof(ssid) - 1);
    // server.arg("p").toCharArray(password, sizeof(password) - 1);
    setSSID(urldecode(server.arg("s").c_str()));
    setPassword(urldecode(server.arg("p").c_str()));

    sendStdHeaderBits();
    server.send(200, "text/html", "");
   
    // Empty content inhibits Content-length header so we have to close the
    // socket ourselves.

    String head = HTTP_HEAD;
    head.replace("{v}", "Credentials Saved");
    server.sendContent(head);
    server.sendContent(HTTP_SCRIPT);
    server.sendContent(HTTP_STYLE);
    server.sendContent(HTTP_HEAD_END);

    server.sendContent(HTTP_SAVED);

    server.sendContent(HTTP_END);
    server.client().stop();

    DEBUG_PRINT("Sent wifi save page");

    // saveCredentials();
    connect = true;             // signal ready to connect/reset
}

void WiFiManager::handle204()
{
    DEBUG_PRINT("204 No Response");
    sendStdHeaderBits();
    server.send(204, "text/plain", "");
}


void WiFiManager::handleNotFound()
{
    if (captivePortal()) {
        // If captive portal redirect instead of
        // displaying the error page.
        return;
    }

    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";

    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    sendStdHeaderBits();
    server.send(404, "text/plain", message);
}


/*
 * Redirect to captive portal if we got a request for another domain. Return
 * true in that case so the page handler do not try to handle the request
 * again. 
 */
boolean WiFiManager::captivePortal()
{
    if (!isIp(server.hostHeader())) {
        DEBUG_PRINTLN("Request redirected to captive portal");
        server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
        server.send(302, "text/plain", "");
        server.client().stop();
        return true;
    }
    return false;
}


/** Is this an IP? */
boolean WiFiManager::isIp(String str)
{
    for (int i = 0; i < str.length(); i++) {
        int c = str.charAt(i);
        if (c != '.' && (c < '0' || c > '9')) {
            return false;
        }
    }
    return true;
}

/** IP to String? */
String WiFiManager::toStringIp(IPAddress ip)
{
    String res = "";
    for (int i = 0; i < 3; i++) {
        res += String((ip >> (8 * i)) & 0xFF) + ".";
    }
    res += String(((ip >> 8 * 3)) & 0xFF);
    return res;
}
