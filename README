
README for avrlirc
=============

avrlirc implements an IR receiver for use with lircd.  It connects
via a serial port, and the included host-based daemon relays the
IR commands to an lircd daemon locally or elsewhere on the network.

AVR part
--------

The first is a simple program (avrlirc.c) that runs on an ATTiny2313
AVR microcontroller, or probably many others, without much work -- an
onboard hardware-based UART is the main requirement.  (The 16-bit timer
and the input capture register are used as well.)  The code monitors the
pulse stream from a standard IR receiver (similar to the Panasonic
PNA4602M or Sharp GP1UD261XK0F), and sends the pulse timing information
to a host computer via the RS232 serial link.  The data format is
designed to closely match that of the LIRC (www.lirc.org) UDP driver.

Host part
----------
The second part of the project is a small program (avrlirc2udp.c) which
reads the avrlirc data stream from the serial port and relays it (via a
UDP socket connection) to a listening lircd daemon.  Such a daemon
would be started using the commandline:

    lircd --driver=udp [ with optional --device=<port-number> ]

Advantages
----------

    - Unlike the traditional LIRC "home-brew" serial port IR dongle, or
	even newer USB-based dongles, there is no requirement here for
	a special kernel driver.  Any serial port capable of at least
	38400 baud can be used, including USB-to-serial converters
	(which can also provide a convenient source of 5V power for the
	AVR micro).

    - Unlike some other AVR-based solutions (which mostly connect via
	USB), there is no need for a new type of config -- the same
	lircd.conf file used for the home-brew receivers will work with
	this.  Nor is there a need for a special kernel driver.

    - Thanks to the UDP network connection the serial port to which the
	avrlirc device is connected need not be on the same machine as
	the lircd process that is interpreting its data.  In my case, I
	have IR receivers attached to several workstations, all of
	which control processes (for home automation, and home audio)
	which run on a central server.  Using avrlirc, the lircd
	daemons can all run on the the central server (each listening
	on a differnt port) which simplifies configuration changes.

Building the code
-----------------
The AVR code was written using avr-gcc (gcc 4.1.1).  The Makefile
assumes a UNIX/Linux system -- if using WinAVR, a new Makefile may well
be required, but its contents are trivial.  A compiled version of the
code (avrlirc-NNNN.hex) is provided for those without the avr-gcc
toolchain -- it was built to be run on an ATTiny2313 using the
calibrated internal 8Mhz oscillator.  Loading the hex file into the
microcontroller, however, is beyond the scope of this README.  :-)

The avrlirc2udp program assumes POSIX termio access for controlling
access to the serial port.  I have no idea how one might accomplish
this control using win32, though I'm sure it must be possible.

There's another program I wrote, based on avrlirc2udp, which is useful
if you have a certain model of IR wireless keyboard, since it handles
that keyboard as well.  If you have a LiteOn Airboard SK-7100, made by
Silitek, you might want to look at: http://airboard-ir.sourceforge.net

Hardware
--------
See the top of avrlirc.c for a pinout diagram.  Other than 5V and
ground, the only connections are the IR receiver, one jumper between
two pins on the AVR, and the data output line.  An RS232 line driver
probably isn't necessary, since most consumer-grade serial ports these
days will accept TTL-level pseudo-RS232 signalling.  But a line driver
like the Maxim MAX233 can be used if necessary.

It's possible that the 38400 baud rate may be too slow to fully support
some IR remotes, which may have shorter bit pulses, or faster button
repeat rates.  In this case, a crystal and capacitors will be necessary
to run the AVR at a rate (like 11.0592 or 14.7456Mhz) which accurately
supports higher speeds.  At higher speeds, you'll probably need to use
the atmel's txd directly, connected via an RS232 line driver (max232 or
equiv).

If pin 7 on the ATtiny2313 is grounded, the code will emit a stream of
'U' characters on the serial port, useful for debugging the
transmission path.

Background
----------

The code contained here is a follow-on to the avrlirc-0.0.5 project
released by Karl Bongers sometime in 2002.  This version was initially
written in April and May of 2007 by Paul Fox.  It bears little relationship
to Karl's initial code, but his code was an invaluable jumpstart.


Paul Fox, May 9, 2007  
(minor revision, Feb 9, 2009)  
(re-hosted at github, April 2013)  
pgf@foxharp.boston.ma.us

