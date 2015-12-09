/*
 * =======================================================================
 * ESP-01 TIX clock
 *
 * Author: Pete Cervasio <cervasio@airmail.net>
 * Date  : 21 SEP 2015
 * 
 * Module: This is a TIX clock using the ESP8266 WiFi chip, in the
 * form of the ESP-01 module.  Since there are only two digital
 * I/O pins available, the options for controlling 27 LEDs are
 * somewhat limited.
 *
 * In this case, we are talking to an MCP23017 16 bit port expander.  It
 * is accessed using gpio0 as the clock line and gpio2 as the data line.
 * The base address of 0x20 is being used.
 * 
 * Bits 0-7 of port A, and bit 0 of port B are being used as column bits
 * while bits 1, 2 and 3 of port B are being used as the row bits.  As
 * there is no PWM control, there is no brightness adjustment.
 * 
 * Bits 4-7 of port B are used as inputs to read 4 switches.  Alarm On
 * toggle, Alarm Set, Alarm Hour+ and Alarm Minute+.
 *
 * Clock logic:
 *
 * The time is retrieved via NTP to 'pool.us.ntp.org'.  A new server is
 * requested any time no response is received from the old server.  The 
 * time server is requeried after one minute at start, which changes to
 * to once per five minutes * successful queries (up to 30 minutes).
 * This is a simple NTP app that doesn't deal with drift.  Whatever the
 * ntp server says the time is, that's what is displayed.
 * 
 * The Time.cpp library is slightly modified to provide a way to set the
 * initial millisecond of the time.  The sync provider callback uses a
 * unsigned long * msec parameter to pass that in.  This makes the clock
 * setting very much more accurate than without, where it can be up to
 * 999 msec off.  When comparing to the time displayed by my NTP synced 
 * Linux desktop, the time changes appear simultaneous.
 * 
 * The WiFiManager library is used to make WiFi setup painless.  The
 * access point and password get stored in eeprom.  If the device can't
 * connect at startup, it sets up a web server on 192.168.4.1 which can
 * be connected to and used to set up the wifi parameters.  This was
 * also modified to 
 * 
 * The Timezone library works with Time and makes DST painless.
 * 
 * At power on, after display test if AlarmSet, Hour+ and Min+ are down, 
 * the current wifi settings are cleared.  Noted by a LED chase display.
 * 
 * TODO: Save alarm time to EEPROM
 *       Work out how to have an alarm beeper (out of pins!)
 *
 * TODO DONE:
 *
 *      Code is at https://github.com/telanoc/esp_tix_clock
 *
 *      Boards at https://www.oshpark.com/shared_projects/UMcsDmIr
 *                https://www.oshpark.com/shared_projects/IgKxO56E
 * 
 * ----------------------------------------------------------------------
 */

#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <WiFiUdp.h>
#include <Wire.h>

// NOTE: Time library modified to pass in partial seconds from UDP packet receive
#include <Time.h>
#include <Timezone.h>

#define TIX_DEBUG

// ESP pins we will use to talk to the i2c bus.  Clock is on
// GPIO0 and data is on GPIO2.
#define SCL_PIN 0
#define SDA_PIN 2

// Address of the MCP23017 i2c port expander on the i2c bus
// This project uses the base address.
#define mcp23017 0x20

// MCP23017 control register definitions
#define REG_MCP_IODIRA   0x00
#define REG_MCP_IODIRB   0x01
#define REG_MCP_IPOLA    0x02
#define REG_MCP_IPOLB    0x03
#define REG_MCP_GPINTENA 0x04
#define REG_MCP_GPINTENB 0x05
#define REG_MCP_DEFVALA  0x06
#define REG_MCP_DEFVALB  0x07
#define REG_MCP_INTCONA  0x08
#define REG_MCP_INTCONB  0x09
#define REG_MCP_IOCONA   0x0A
#define REG_MCP_IOCON    REG_MCP_IOCONA
#define REG_MCP_IOCONB   0x0B
#define REG_MCP_GPPUA    0x0C
#define REG_MCP_GPPUB    0x0D
#define REG_MCP_INTFA    0x0E
#define REG_MCP_INTFB    0x0F
#define REG_MCP_INTCAPA  0x10
#define REG_MCP_INTCAPB  0x11
#define REG_MCP_GPIOA    0x12
#define REG_MCP_GPIOB    0x13
#define REG_MCP_OLATA    0x14
#define REG_MCP_OLATB    0x15

WiFiManager wifi;
unsigned int localPort = 12525;

IPAddress timeServerIP;
const char *ntpServerName = "us.pool.ntp.org";

WiFiUDP udp;

// If this was a good clock, it'd have every timezone defined and let you pick
// which one you wanted during access point setup.  Guess what... this clock
// isn't 100% good.

//US Central Time Zone (Chicago, Dallas, etc)
// Format: Name, which, day of week, month, hour to change, new offset from utc
TimeChangeRule usCDT = {"CDT", Second, dowSunday, Mar, 2, -300};
TimeChangeRule usCST = {"CST", First, dowSunday, Nov, 2, -360};
Timezone myTZ(usCDT, usCST);
TimeChangeRule *tcr;

int lastHour = -1;
int lastMin  = -1;

time_t lastnow = 0;
time_t utc = 0;
time_t local = 0;

int alarm_hour = 12;
int alarm_minute = 34;

// State of ALARM_SET button
bool showing_alarm = false;

// some globals to store our last hour+/min+ button states
bool lastminp = false;
bool lasthourp = false;

// After read/shift, these are the button bits
#define BIT_ALARM_ON      0
#define BIT_MINUTE_PLUS   1
#define BIT_HOUR_PLUS     2
#define BIT_ALARM_SET     3

// Clock display related time values
#define ROWCOUNT 3
#define COLCOUNT 9

// Bits for the columns of each of the 3 rows
unsigned int LedWords[ROWCOUNT] = { 0, 0, 0 };
// values or'd to turn on row driver transistors
unsigned int rowbits[ROWCOUNT] = { 0x0200, 0x0400, 0x0800 };

unsigned long last_ms = 0;
unsigned long last_update = 0;

int ntp_good_count = 0;

int cur_row = 0;

// These should really be named i2c_read... and i2c_write... as they're
// not specific to a gpio port expander.  One day, when I feel like it.
word gpio_read_word(int address, int regnum);
int  gpio_read_byte(int address, int regnum);
void gpio_write_word(int address, int regnum, word data);
void gpio_write_byte(int address, int regnum, byte data);

// initializes the mcp32017 registers for our purposes
void init_mcp23017(int address);
void mcp_write(int ab, byte value);
void mcp_write2(word value);
byte mcp_read(void);


// Modified Time library callback.  Return time and write millisec of the time
// to *msec.
time_t getNtpTime(unsigned long * msec);

unsigned long sendNTPpacket(IPAddress & address);


//=========================================================================
// Routine: fill_clock_dots
//-------------------------------------------------------------------------
// Parameters:
//   rows    : Number of rows to use in the matrix
//   cols    : How many columns to use in the matrix
//   numdots : How many random values to set to true
//   offset  : Starting column in the matrix
//-------------------------------------------------------------------------
// Purpose: Sets [numdots] random TIX digit LEDs into [cols] colums at an
// offset of [offset].
//=========================================================================
void fill_clock_dots(int rows, int cols, int numdots, int offset)
{
  int row, col;
  int count = 0;

  // Shortcut - if setting all on, just set them all on
  if (numdots >= (rows * cols)) {
    for (row = 0; row < rows; row++) {
      for (col = 0; col < cols; col++) {
        bitSet(LedWords[row], col + offset);
      }
    }
    return;
  }

  while (count < numdots) {
    row = random(rows);
    col = random(cols);
    if (bitRead(LedWords[row], col + offset) == 0) {
      bitSet(LedWords[row], col + offset);
      count++;
    }
  }
}

//=========================================================================
// Routine: update_displayed_time
//-------------------------------------------------------------------------
// Parameters:
//   when : The time_t value to load into the Tix clock LED matrix
//-------------------------------------------------------------------------
// Purpose:  Update the LedWords[] array with proper information.  Will be
// called every time the hours or minutes change, and whenever a new set
// of random leds is wanted (every 5 seconds at initial writing)
//=========================================================================
void update_displayed_time (time_t when)
{  
  LedWords[0] = LedWords[1] = LedWords[2] = 0;

  int hh = hour(when);
  int mm = minute(when);

  fill_clock_dots (3, 1, hh / 10, 0);
  fill_clock_dots (3, 3, hh % 10, 1);
  fill_clock_dots (3, 2, mm / 10, 4);
  fill_clock_dots (3, 3, mm % 10, 6);
}

//=========================================================================
// Routine: update_displayed_alarm
//-------------------------------------------------------------------------
// Parameters:
//   none
//-------------------------------------------------------------------------
// Purpose:  Update the LedWords[] array with the alarm time.  
//=========================================================================
void update_displayed_alarm()
{
  LedWords[0] = LedWords[1] = LedWords[2] = 0;  /* never forget to do this */
  
  fill_clock_dots (3, 1, alarm_hour / 10, 0);
  fill_clock_dots (3, 3, alarm_hour % 10, 1);
  fill_clock_dots (3, 2, alarm_minute / 10, 4);
  fill_clock_dots (3, 3, alarm_minute % 10, 6);
}


//=========================================================================
// Routine: show_time_row
//-------------------------------------------------------------------------
// Parameters: 
//   row: Which row of data to send, obviously
//-------------------------------------------------------------------------
// Purpose: Send out a row of data to the multiplexed LED array.
//=========================================================================
void show_time_row(int row)
{
  mcp_write2(LedWords[row] | rowbits[row]);
}


//=========================================================================
// Routine: setup
//-------------------------------------------------------------------------
// Parameters:
//   none
//-------------------------------------------------------------------------
// Purpose: Initialization code for the clock.
//=========================================================================
void setup()
{
  // put your setup code here, to run once:  (okay, thanks Arduino)
  
  Serial.begin(115200);
  Serial.println();
  Serial.println("-----------------------------------------------");
  Serial.println("Initializing MCP23017 i/o expander");

  Wire.begin(SDA_PIN, SCL_PIN);
  init_mcp23017(mcp23017);
  mcp_write2(0);

  Serial.println("Starting LED test");
  for (int b = 0; b < COLCOUNT; b++) {
    // This should use math and ROWCOUNT, not hard coding
    mcp_write2((1 << b) | rowbits[0] | rowbits[1] | rowbits[2] );
    delay (200);
  }
  // This bit too.
  mcp_write2(0x1ff | rowbits[0] | rowbits[1] | rowbits[2]);
  delay (1000);
  mcp_write2(0);
  delay (100);
  
  word btns = mcp_read() >> 4;
  if (btns == 0x0e) {
    // Hour/Min set and Alarm Set pressed at startup - clear Wifi data
    wifi.resetSettings();
    // And do a dance on the LEDs so I know it happened.
    for (int i = 0; i < 5; i++) {
      for (int b = 0; b < COLCOUNT; b++) {
        mcp_write2((1 << b) | rowbits[1] );
        delay (100);
      }
      for (int b = COLCOUNT - 2; b > 0; b--) {
        mcp_write2((1 << b) | rowbits[1] );
        delay (100);
      }
    }      
    mcp_write2(0);
  }

  Serial.println("Initializing WiFi connection");

  // WiFiManager fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // (here "Tix_Clock") and goes into a blocking loop awaiting configuration
  wifi.autoConnect("Tix_Clock");
  yield();

  // We must be connected now.
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  yield();

  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP); 
  Serial.print ("Ntp server: ");
  Serial.println (timeServerIP);
  yield();
  
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  yield();
  setSyncInterval (60);
  setSyncProvider(getNtpTime);

  while (timeStatus() != timeSet) {
    mcp_write2(0x1ff | rowbits[0] | rowbits[2]);
    delay (500);
    setSyncProvider(getNtpTime);
  }
  mcp_write2(0);
  
  delay (200);
  Serial.println("TiX clock initialization complete.  Enjoy your time.");
  Serial.println("----------------------------------------------------");
  Serial.println();

  // initialize the "last time" value
  last_ms = millis();
}


//=========================================================================
// Routine: loop
//-------------------------------------------------------------------------
// Parameters:
//-------------------------------------------------------------------------
// Purpose: "main" program loop.  Called over and over repeatedly.
//=========================================================================
void loop()
{
  unsigned long curr_ms = millis();

  // update the clock parameters every 50 msec.  This also
  // takes care of our switch debouncing while we're at it.
  
  if (curr_ms - last_update > 50)
  {
    // read button bits
    word btns = mcp_read() >> 4;

    if (showing_alarm != bitRead(btns, BIT_ALARM_SET)) {
      showing_alarm = bitRead(btns, BIT_ALARM_SET);
      if (showing_alarm) {
        // Transfer alarm time to display bits
        update_displayed_alarm(); 
      } else {
        // We must have let go of the button... force the clock
        // logic to immediately update the LEDs
        lastHour = -1;
      }
    }
    
    if (showing_alarm) {
      
      // Alarm_Set button down.  Handle adjust buttons.  The
      // alarm time only gets random LEDs when the alarm time
      // changes, not every {x} seconds.
      
      if (lastminp != bitRead (btns, BIT_MINUTE_PLUS)) {
        lastminp = bitRead (btns, BIT_MINUTE_PLUS);
        if (lastminp) {
          alarm_minute = (alarm_minute + 1) % 60;
          update_displayed_alarm();
        }
      }

      if (lasthourp != bitRead (btns, BIT_HOUR_PLUS)) {
        lasthourp = bitRead (btns, BIT_HOUR_PLUS);
        if (lasthourp) {
          alarm_hour = (alarm_hour + 1) % 24;
          update_displayed_alarm();
        }
      }
    } else {
      
      // AlarmSet button not down, deal with display of the current time
      
      local = myTZ.toLocal(now(), &tcr);
      int aHour = hour(local);
      int aMin  = minute(local);
      int aSec  = second(local);
      if ((aHour != lastHour) || (aMin != lastMin) || (aSec % 5 == 0 && lastnow != local) ) {   
        update_displayed_time (local);
        // Only print serial time every 15 seconds
        if (aSec % 15 == 0) 
          printTime (local, tcr->abbrev); 
  
        lastnow = local;
        lastHour = aHour;
        lastMin = aMin;
      }
    }
    last_update = curr_ms;
  }

  // update the display all the time
  show_time_row(cur_row);
  cur_row = (cur_row + 1) % ROWCOUNT;
  delay (5);
  mcp_write2(0);  
}


//=========================================================================
// Routine: init_mcp23017
//-------------------------------------------------------------------------
// Parameters:
//   address: bus address of the device on the i2c bus
//-------------------------------------------------------------------------
// Purpose: Configure mcp23017 properly for our application
//=========================================================================
void init_mcp23017(int address)
{
    // Init IO Config - set the following bits
    //    INTPOL (bit 1) = 1    (int output active-high)
    //    ODR    (bit 2) = 0    (use active output)
    //    MIRROR (bit 6) = 1    (both INT pins the same value)

    // 12 OCT 2015: [PWC] - don't need these in this project.
    //byte iocon = (1 << 1) | (1 << 6);
    //gpio_write_byte (address, REG_MCP_IOCON, iocon);
    //gpio_write_byte (address, REG_MCP_IOCONB, iocon);

    // Set A and half of B as outputs
    gpio_write_byte(address, REG_MCP_IODIRA, 0x00);
    gpio_write_byte(address, REG_MCP_IODIRB, 0xf0);

    // Force normal polarity
    gpio_write_word(address, REG_MCP_IPOLA, 0xffff);
    //gpio_write_byte(address, REG_MCP_IPOLB, 0xff);

    // input defaults
    gpio_write_word (address, REG_MCP_DEFVALB, 0xF0);

    // enable 100k pullups on switch inputs, please
    gpio_write_byte (address, REG_MCP_GPPUB, 0xf0);

}

//=========================================================================
// Routine: gpio_read_word
//-------------------------------------------------------------------------
// Parameters:
//-------------------------------------------------------------------------
// Purpose: Read 16 bits of information from expander chip input ports
//=========================================================================
word gpio_read_word(int address, int regnum)
{
    int data = 0;

    //  Send input register address
    Wire.beginTransmission(address);
    Wire.write(regnum);
    Wire.endTransmission();

    //  Connect to device and request two bytes
    Wire.beginTransmission(address);
    Wire.requestFrom(address, 2);

    if (Wire.available()) {
        data = Wire.read();
    }
    if (Wire.available()) {
        data |= Wire.read() << 8;
    }

    Wire.endTransmission();

    return data;
}

//=========================================================================
// Routine: gpio_read_word
//-------------------------------------------------------------------------
// Parameters:
//-------------------------------------------------------------------------
// Purpose: Read a byte from any port expander chip register
//=========================================================================
int gpio_read_byte(int address, int regnum)
{
    int data = 0;

    //  Send input register address
    Wire.beginTransmission(address);
    Wire.write(regnum);
    Wire.endTransmission();

    //  Connect to device and request two bytes
    Wire.beginTransmission(address);
    Wire.requestFrom(address, 1);

    if (Wire.available()) {
        data = Wire.read();
    }

    Wire.endTransmission();

    return data;
}

//=========================================================================
// Routine: gpio_write_word
//-------------------------------------------------------------------------
// Parameters:
//-------------------------------------------------------------------------
// Purpose: Write 16 bits to the port expander digital outputs
//=========================================================================
void gpio_write_word(int address, int regnum, word data)
{
    //  Send output register address
    Wire.beginTransmission(address);
    Wire.write(regnum);

    //  Connect to device and send two bytes
    Wire.write(0xff & data);    //  low byte
    Wire.write(data >> 8);      //  high byte

    Wire.endTransmission();
}


//=========================================================================
// Routine: gpio_write_byte
//-------------------------------------------------------------------------
// Parameters:
//-------------------------------------------------------------------------
// Purpose: Write a byte to any port expander register
//=========================================================================
void gpio_write_byte(int address, int regnum, byte data)
{
    //  Send output register address
    Wire.beginTransmission(address);
    Wire.write(regnum);

    //  Connect to device and send byte
    Wire.write(0xff & data);

    Wire.endTransmission();
}


//=========================================================================
// Routine: mcp_write()
//-------------------------------------------------------------------------
// Parameters:
//   ab: 0 = port a, 1 = port b
//   value: what value to write
//-------------------------------------------------------------------------
// Purpose: Write a byte value to the GPIO outputs, either port A or B.
//=========================================================================
void mcp_write(int ab, byte value)
{
    gpio_write_byte(mcp23017, REG_MCP_GPIOA + ab, value);
}

//=========================================================================
// Routine: mcp_write2()
//-------------------------------------------------------------------------
// Parameters:
//   value: 16 bit word value to write
//-------------------------------------------------------------------------
// Purpose: Write to both GPIO outputs at once.
//=========================================================================
void mcp_write2(word value)
{
    gpio_write_word(mcp23017, REG_MCP_GPIOA, value);
}


//=========================================================================
// Routine: mcp_read()
//-------------------------------------------------------------------------
// Parameters:
//   none
//-------------------------------------------------------------------------
// Purpose: Read port B data from the mcp 23017 port expander.  
//=========================================================================
byte mcp_read(void)
{
  return gpio_read_byte (mcp23017, REG_MCP_GPIOB);
}


//=========================================================================
// Routine: printTime
//-------------------------------------------------------------------------
// Parameters:
//   t : Time to display
//  *tz: timezone
//-------------------------------------------------------------------------
// Purpose: Send time to the serial port
//=========================================================================
//Function to print time with time zone
void printTime(time_t t, char *tz)
{
    sPrintI00(hour(t));
    sPrintDigits(minute(t));
    sPrintDigits(second(t));
    Serial.print(' ');
    Serial.print(dayShortStr(weekday(t)));
    Serial.print(' ');
    sPrintI00(day(t));
    Serial.print(' ');
    Serial.print(monthShortStr(month(t)));
    Serial.print(' ');
    Serial.print(year(t));
    Serial.print(' ');
    Serial.print(tz);
    Serial.println();
}

//=========================================================================
// Routine: sPrintI00()
//-------------------------------------------------------------------------
// Parameters:
//   val: value to write
//-------------------------------------------------------------------------
// Purpose: serial print an integer in "00" format (with leading zero).
// Input value assumed to be between 0 and 99.
//=========================================================================
void sPrintI00(int val)
{
    if (val < 10) Serial.print('0');
    Serial.print(val, DEC);
}

//=========================================================================
// Routine: sPrintDigits()
//-------------------------------------------------------------------------
// Parameters:
//   val: The value to print
//-------------------------------------------------------------------------
// Purpose:  Serial print an integer in ":00" format (with leading zero).
// Input value assumed to be between 0 and 99.
//=========================================================================
void sPrintDigits(int val)
{
    Serial.print(':');
    sPrintI00(val);
}

//-------------------------------------------------------------------------
// "private" data for the ntp connection

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
unsigned long ntp_extramillis = 0;

//=========================================================================
// Routine: getNtpTime  <--- NON STANDARD - has a parameter.
//-------------------------------------------------------------------------
// Parameters:
//   *msec : Pointer to get number of milliseconds in the time.
//-------------------------------------------------------------------------
// Purpose: Queries the NTP server and waits for a response.  The wait is
// up to 4.5 seconds.  If no response is received in that time then a new
// time server address is requested. 
//-------------------------------------------------------------------------
// Returns: UTC time value from the NTP server (or zero)
//=========================================================================
// Called by the Time library when it thinks it's time to synchronize the time

time_t getNtpTime(unsigned long *msec)
{

  /*  Was doing this during debugging - it's annoying really
  // Turn on the middle row of the clock while we're doing this
  //mcp_write2(0x1ff | rowbits[1]);
  */
  
  // discard any previously received packets
  while (udp.parsePacket() > 0)
    yield(); 
    
#ifdef TIX_DEBUG
  Serial.println("Transmit NTP Request");
#endif

  sendNTPpacket(timeServerIP);
  
  uint32_t beginWait = millis();
  while (millis() - beginWait < 4500) {
    
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      
#ifdef TIX_DEBUG
      Serial.print("Receive NTP Response: ");
#endif
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      
      unsigned long secsSince1900;
      
      // convert four bytes starting at location 40 to a long integer
      secsSince1900  = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      
      *msec  = (unsigned long)packetBuffer[44] << 8;
      *msec |= (unsigned long)packetBuffer[45];
      *msec  = map (*msec, 0, 0xFFFF, 0, 999);

      if (ntp_good_count < 6) {
        ntp_good_count++;
        if (ntp_good_count > 1)
          setSyncInterval (300 * ntp_good_count - 1);
      }

#ifdef TIX_DEBUG
      Serial.print(secsSince1900 - 2208988800UL);
      Serial.print(" . ");
      Serial.println(*msec);
#endif
      return secsSince1900 - 2208988800UL;
      
    }
    delay(1);
  }
  
#ifdef TIX_DEBUG
  Serial.println("No NTP response.  ");
#endif
  // When there's no response, just get a new server from the pool
  // and try again.

  ntp_good_count = 0;
  
  WiFi.hostByName(ntpServerName, timeServerIP);
  
  Serial.print ("New server: ");
  Serial.println (timeServerIP);

  // Try again in a little while.
  setSyncInterval (30);
  
  return 0; // return 0 if unable to get the time

}

//=========================================================================
// Routine: sendNTPpacket
//-------------------------------------------------------------------------
// Parameters:
//   address : The IP address of the NTP server to query
//-------------------------------------------------------------------------
// Purpose: Initialize and transmit the NTP query to the server
//=========================================================================
// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress & address)
{
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;      // LI, Version, Mode
    packetBuffer[1] = 0;        // Stratum, or type of clock
    packetBuffer[2] = 6;        // Polling Interval
    packetBuffer[3] = 0xEC;     // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    udp.beginPacket(address, 123);      //NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}

