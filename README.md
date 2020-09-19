# XNotify

XNotify displays a notification on the screen.
XMenu receives a notification specification in stdin and shows a
notification for the user on the screen.


## Features

XNotify comes with the following features:

* Image support (see below).
* Support for fallback fonts (you can set more than one fonts, that will be tried in order).
* Queue of notifications (each notification is displayed above the previous one).
* X resources support (you don't need to recompile Xnotify for configuring it).


## Files

The files are:

* `./README`:           This file.
* `./Makefile`:         The makefile.
* `./config.h`:         The hardcoded default configuration for XNotify.
* `./config.mk`:        The setup for the makefile.
* `./xnotify.{c,h}`:    The source code of XNotify.


## Installation

First, edit `./config.mk` to match your local setup.

In order to build XNotify you need the `Imlib2`, `Xlib` and `Xft` header files.
The default configuration for XNotify is specified in the file `config.h`,
you can edit it, but most configuration can be changed at runtime via
X resources and via command-line options.
Enter the following command to build XNotify.
This command creates the binary file `./xnotify`.

	make

By default, XNotify is installed into the `/usr/local` prefix.  Enter the
following command to install XNotify (if necessary as root).  This command
installs the binary file `./xnotify` into the `${PREFIX}/bin/` directory, and
the manual file `./xnotify.1` into `${MANPREFIX}/man1/` directory.

	make install


## Running XNotify

XMenu receives as input a notification specification where each line is
a notification entry.  Each line is made out of a notification title and
a notification body separated by any number of tabs.  Lines without
title are ignored.

The following is an example of how to run XNotify

	$ </tmp/xnotify.fifo xnotify -m 0 -G NE -g -10+10 -s 15

This line means: read notifications from `/tmp/xnotify.fifo`, display
the notifications on the north east (`-G NE`) of the monitor 0 (`-m 0`),
that is, on the upper right corner of the first monitor.  The
notifications should be placed -10 pixels to the left and +10 pixels
down (thus creating a 10 pixel gap with the upper right corner).
Each notification stay alive for 15 seconds.

To create a fifo for XNotify, you can place the following in your `~/.xinitrc`:

	mkfifo /tmp/xnotify.fifo
	while true
	do
		[ -p /tmp/xnotify.fifo ] || break
		cat /tmp/xnotify.fifo
		sleep 3
	done > /tmp/xnotify.fifo &
