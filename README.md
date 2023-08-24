# XNotify

![demo](https://user-images.githubusercontent.com/63266536/93797439-c65d0680-fc2b-11ea-80e3-10bbd6f65dcb.gif)

XNotify displays a notification on the screen.
XNotify receives a notification specification in stdin and shows a
notification for the user on the screen.

XNotify comes with the following features:

* XNotify receives notifications from stdin.
  You can use a fifo to echo notifications on the fly like
  `echo Hello World > /path/to/xnotify.fifo`
* XNotify queues notifications and displays them one above the other.
* Image support, just prefix the notification string with
  `IMG:/path/to/the/file.png` and a tab.
* Multiple monitor support.  You can set the monitor with the `-m` option.
* Support for fallback fonts (you can set more than one fonts, that will
  be tried in order).
* X resources support (you don't need to recompile Xnotify for
  configuring it).

## Options

XNotify understands the following command-line options:

* `-b button`:  Specify the action button.
* `-g gravity`: Specify the screen corner/border to place notifications at.
* `-h height`:  Specify the maximum height of a notification popup.
* `-m monitor`: Specify the monitor to place notifications at.
* `-o`:         Only one notification at a time.
* `-r`:         Also read notifications from root window name (in
                addition to read from standard input).
* `-s seconds`: Specify the time in seconds notifications are visible.
* `-w`:         Let the window manager control notification popups.

## Customization

XNotify can be customized by setting the following X resources.

* `alignment`:   Text alignment (left, center, or right).
* `background`:  Background color.
* `borderColor`: Border color.
* `borderWidth`: Border width in pixels.
* `faceName`:    Font.
* `foreground`:  Text color.
* `gap`:         Gap between notifications, in pixels.
* `gravity`:     Screen corner/border to place notifications at.
* `leading`:     Space between lines.
* `maxHeight`:   Maximum notification height.
* `opacity`:     Notification opacity from 0.0 to 1.0.
* `shrink`:      Whether to shrink notifications to its content size.
* `padding`:     Margin around the content.
* `wrap`:        Whether to wrap long lines.


## Installation

Run `make all` to build, and `make install` to install the binary and the
manual into `${PREFIX}` (`/usr/local`).

## Usage

XNotify receives as input one line per notification.
Each line is made out of a notification title and a notification body separated by any number of tabs.
Lines without a title are ignored.

The following is an example of how to run XNotify

	$ xnotify -m 10 -G NE -g -10+10 -s 15

This line means: read notifications from stdin, display
the notifications on the north east (`-G NE`) of the monitor 0 (`-m 0`),
that is, on the upper right corner of the first monitor.  The
notifications should be placed 10 pixels to the left and 10 pixels
down (thus creating a 10 pixel gap with the upper right corner).
Each notification stay alive for 15 seconds.

To create a named pipe for XNotify, you can place the following in the beginning of your `~/.xinitrc`.
This will create a named pipe unique to your current X display in your home directory at `~/.cache`.
Then, it will open xnotify in the background, reading from this named pipe.

	XNOTIFY_FIFO="$HOME/.cache/xnotify$DISPLAY.fifo"
	export XNOTIFY_FIFO
	rm -f $XNOTIFY_FIFO
	mkfifo $XNOTIFY_FIFO
	xnotify 0<>$XNOTIFY_FIFO

Note that the first two lines (the line setting the environment variable and the line exporting it)
should be at the beginning of your `~/.xinitrc`, so other programs you invoke are aware of this variable.

To create a notification with a image, input to XNotify a line beginning
with `IMG:/path/to/file.png` followed by a tab.  For example:

	$ printf 'IMG:/path/to/file.png\tThis is a notification\n' > $XNOTIFY_FIFO

To read dbus notifications from stdin, you'll need [tiramisu](https://github.com/Sweets/tiramisu).
Then add the following line to your `.xinitrc`, after the line calling xnotify.

	$ tiramisu -o "$(printf '#summary\t#body\n')" > $XNOTIFY_FIFO &

To use a different size other than the default for the notifications,
run `xnotify` with the `-g` option set to the notification size in
`WIDTHxHEIGHT`.  For example:

	$ xnotify -g 300x80

The argument for the `-g` option has the form `[WIDTHxHEIGHT][{+-}XPOS{+-}YPOS]`.
Parts between square brackets are optional.
`{+-}` means to chose either `+` or `-`.

## License

The code and manual are under the MIT/X license.
See ./LICENSE for more information.

## Epilogue

**Read the manual.**
