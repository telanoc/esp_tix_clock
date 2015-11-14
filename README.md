=========================================================================

Module: esp tix clock

Date: 13 November 2015

Author: Pete Cervasio <cervasio@airmail.net>

=========================================================================

This project is a TIX clock running on an ESP-01 module.  Since the
I/O is limited, the two available pins are used for an i2c bus to
drive a port expander.  That handles the 27 LEDs as a 9x3 matrix usign
a ULN2803 and another transistor for the columns and three transistors
to drive the rows.  The other 4 pins read mostly-useless alarm setting
buttons.  If only there was another output to use to drive some kind
of alarmy soundy thing.  Oops.

So, what's a TIX clock?  Google is your friend, as are Bing and Yahoo.
And Youtube to an extent.

Features:

	ESP-01 module gets time from NTP - All you set is the access point
	& password.  You set the timezone before you compile.  Automatic
	changeover takes over from there.  Just don't move around a lot.
	:)

	Power is +5v, regulated to +3.3 for the ESP-01.  The clock in my
	living room is powered by your generic three-for-a-dollar USB
	power brick.

	Time library used to get NTP.  Modifications were made to make the
	time setting closer to msec accuracy (it's pretty close).  I got
	this from https://github.com/PaulStoffregen/Time if you want the
	original.

	WiFiManager used to auto-connect to the access point, and to set
	up the connection if necessary.  From AlexT
	(https://github.com/tzapu) but custom changes are stored in this
	repository.

	Timezone library (https://github.com/JChristensen/Timezone) used
	to handle automatic DST change.

	Display and Controller boards designed in Eagle 7.4 are included.
	The two boards are shared on Osh Park

		Display: https://www.oshpark.com/shared_projects/UMcsDmIr
	 Controller: https://www.oshpark.com/shared_projects/IgKxO56E

	An alarm time can be set, but "oops, I need more pins"... there's
	no alarm output.  So it's not much of a feature after all.

	A dumb 3d printed case will eventually be available on Tinkercad
	or Thingiverse or somewhere.  The one I have started is close but
	not quite there yet.

Caveats and whatnot:

	This was built using staging release esp8266-1.6.5-1160-gef26c5f

	Soldering SMTs by hand is a pain in the behind when you're older.

	Don't bring pin 9 of the display board to +5v without a current
	limiting resistor or you'll take out the 2n3904.  Don't ask how I
	know that...  the resistor on the control board should have been
	moved.
    
	... ahh hindsight.

	male Power pins are long out the bottom of the control board to
	allow attaching a header for +5v.  That's how I did it.  If you
	have a better idea, please use it.  :)

Todo:

	Build a nice 3d printed case at the North Richland Hills Library
	Maker Spot.  I can't say enough good things about the most
	important building in NRH (and the highly important room upstairs
	that has all the fun makey bits in it).  If you live around Fort
	Worth, go visit.  It's a "soft" maker space, with no table saws or
	welder or whatnot, but that's what the Dallas maker space is good
	for.  :)

	Revamp the boards to bring out serial data, which would allow for
	setting more parameters and storing them in eeprom, like time zone
	values and stuff like that.

	Move that transistor base resistor to the display board.

Harware BOM:  This should be pretty accurate.

        1 - ESP-01
        1 - mcp23017 (so28w)
        1 - uln2803  (so18w)
        4 - MMBT3904 (sot23-bec)
        2 - BSS138 mosfets (sot-23)
        4 - standard through-hole tactile switches
        1 - AP2112K-3.3 regulator (sot-23-5)
        1 - 10uf 0805 capacitor
        2 - 0.1 uf 0805 capacitor
        1 - 1k 0805 resistor
        6 - 10k 0805 resistor
       27 - 68 ohm 0805 resistors
        3 - AA2214SURSK SMT blue LEDs
        6 - yellow SMT LEDs
        9 - red SMT LEDs
        9 - green SMT LEDs
        1 - 1x2 pin male header (extra long) 
        1 - 1x3 pin male header
        1 - 1x3 pin mail header
        1 - 1x3 female header
        1 - 1x2 female header
        1 - 1x9 female header
        1 - 2x4 female header


