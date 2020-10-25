#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xinerama.h>
#include <Imlib2.h>
#include "xnotify.h"

/* macros */
#define DEFWIDTH            350     /* default width of a notification */
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))

/* X stuff */
static Display *dpy;
static Colormap colormap;
static Visual *visual;
static Window root;
static XrmDatabase xdb;
static char *xrm;
static int screen;
static int depth;
static int xfd;
static struct Monitor mon;
static Atom utf8string;
static Atom netatom[NetLast];
static struct DC dc;
static struct Fonts titlefnt, bodyfnt;

/* flags */
static int oflag = 0;   /* whether only one notification must exist at a time */

/* include configuration structure */
#include "config.h"

/* show usage */
void
usage(void)
{
	(void)fprintf(stderr, "usage: xnotify [-o] [-G gravity] [-g geometry] [-m monitor] [-s seconds]\n");
	exit(1);
}

/* get configuration from X resources */
static void
getresources(void)
{
	XrmValue xval;
	unsigned long n;
	char *type;

	if (xrm == NULL || xdb == NULL)
		return;

	if (XrmGetResource(xdb, "xnotify.title.font", "*", &type, &xval) == True)
		config.titlefont = xval.addr;
	if (XrmGetResource(xdb, "xnotify.body.font", "*", &type, &xval) == True)
		config.bodyfont = xval.addr;
	if (XrmGetResource(xdb, "xnotify.background", "*", &type, &xval) == True)
		config.background_color = xval.addr;
	if (XrmGetResource(xdb, "xnotify.foreground", "*", &type, &xval) == True)
		config.foreground_color = xval.addr;
	if (XrmGetResource(xdb, "xnotify.border", "*", &type, &xval) == True)
		config.border_color = xval.addr;
	if (XrmGetResource(xdb, "xnotify.geometry", "*", &type, &xval) == True)
		config.geometryspec = xval.addr;
	if (XrmGetResource(xdb, "xnotify.gravity", "*", &type, &xval) == True)
		config.gravityspec = xval.addr;
	if (XrmGetResource(xdb, "xnotify.borderWidth", "*", &type, &xval) == True)
		if ((n = strtoul(xval.addr, NULL, 10)) < INT_MAX)
			config.border_pixels = n;
	if (XrmGetResource(xdb, "xnotify.gap", "*", &type, &xval) == True)
		if ((n = strtoul(xval.addr, NULL, 10)) < INT_MAX)
			config.gap_pixels = n;
	if (XrmGetResource(xdb, "xnotify.imageWidth", "*", &type, &xval) == True)
		if ((n = strtoul(xval.addr, NULL, 10)) < INT_MAX)
			config.image_pixels = n;
	if (XrmGetResource(xdb, "xnotify.leading", "*", &type, &xval) == True)
		if ((n = strtoul(xval.addr, NULL, 10)) < INT_MAX)
			config.leading_pixels = n;
	if (XrmGetResource(xdb, "xnotify.padding", "*", &type, &xval) == True)
		if ((n = strtoul(xval.addr, NULL, 10)) < INT_MAX)
			config.padding_pixels = n;
	if (XrmGetResource(xdb, "xnotify.shrink", "*", &type, &xval) == True)
		config.shrink = (strcasecmp(xval.addr, "true") == 0 ||
		                strcasecmp(xval.addr, "on") == 0 ||
		                strcasecmp(xval.addr, "1") == 0);
	if (XrmGetResource(xdb, "xnotify.alignment", "*", &type, &xval) == True) {
		if (strcasecmp(xval.addr, "center") == 0)
			config.alignment = CenterAlignment;
		else if (strcasecmp(xval.addr, "right") == 0)
			config.alignment = RightAlignment;
	}
}

/* get configuration from commmand-line */
static void
getoptions(int argc, char *argv[])
{
	unsigned long n;
	int ch;

	while ((ch = getopt(argc, argv, "G:g:m:os:")) != -1) {
		switch (ch) {
		case 'G':
			config.gravityspec = optarg;
			break;
		case 'g':
			config.geometryspec = optarg;
			break;
		case 'm':
			mon.num = atoi(optarg);
			break;
		case 'o':
			oflag = 1;
			break;
		case 's':
			if ((n = strtoul(optarg, NULL, 10)) < INT_MAX)
				config.sec = n;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();
}

/*
 * get XftColor *color from color string s
 * if the variable error is nonzero, exit on error
 * if the variable error is zero, return -1 on error
 */
static int
ealloccolor(const char *s, XftColor *color, int error)
{
	if (!XftColorAllocName(dpy, visual, colormap, s, color)) {
		if (error)
			errx(1, "could not allocate color: %s", s);
		warnx("could not allocate color: %s", s);
		return -1;
	}
	return 0;
}

/* get number from *s into n, return 1 if error, update s to end of number */
static int
getnum(const char **s, int *n)
{
	int retval;
	long num;
	char *endp;

	num = strtol(*s, &endp, 10);
	retval = (errno == ERANGE || num > INT_MAX || num < 0 || endp == *s);
	*s = endp;
	*n = num;
	return retval;
}

/* parse geometry specification and return geometry values */
static void
parsegeometryspec(int *x, int *y, int *w, int *h)
{
	int sign;
	int n;
	const char *s;

	*x = *y = *w = *h = 0;
	s = config.geometryspec;

	if (*s != '+' && *s != '-') {
		/* get *w */
		if (getnum(&s, &n))
			goto error;
		if (*s == '%') {
			if (n > 100)
				goto error;
			*w = (n * (mon.w - config.border_pixels * 2))/100;
			s++;
		} else {
			*w = n;
		}
		if (*s++ != 'x')
			goto error;

		/* get *h */
		if (getnum(&s, &n))
			goto error;
		if (*s == '%') {
			if (n > 100)
				goto error;
			*h = (n * (mon.h - config.border_pixels * 2))/100;
			s++;
		} else {
			*h = n;
		}
	}

	if (*s == '+' || *s == '-') {
		/* get *x */
		sign = (*s++ == '-') ? -1 : 1;
		if (getnum(&s, &n))
			goto error;
		*x = n * sign;
		if (*s != '+' && *s != '-')
			goto error;

		/* get *y */
		sign = (*s++ == '-') ? -1 : 1;
		if (getnum(&s, &n))
			goto error;
		*y = n * sign;
	}
	if (*s != '\0')
		goto error;

	return;

error:
	errx(1, "improper geometry specification %s", config.geometryspec);
}

/* parse gravity specification and return gravity value */
static void
parsegravityspec(int *gravity, int *direction)
{
	if (config.gravityspec == NULL || strcmp(config.gravityspec, "N") == 0) {
		*gravity = NorthGravity;
		*direction = DownWards;
	} else if (strcmp(config.gravityspec, "NW") == 0) {
		*gravity = NorthWestGravity;
		*direction = DownWards;
	} else if (strcmp(config.gravityspec, "NE") == 0) {
		*gravity = NorthEastGravity;
		*direction = DownWards;
	} else if (strcmp(config.gravityspec, "W") == 0) {
		*gravity = WestGravity;
		*direction = DownWards;
	} else if (strcmp(config.gravityspec, "C") == 0) {
		*gravity = CenterGravity;
		*direction = DownWards;
	} else if (strcmp(config.gravityspec, "E") == 0) {
		*gravity = EastGravity;
		*direction = DownWards;
	} else if (strcmp(config.gravityspec, "SW") == 0) {
		*gravity = SouthWestGravity;
		*direction = UpWards;
	} else if (strcmp(config.gravityspec, "S") == 0) {
		*gravity = SouthGravity;
		*direction = UpWards;
	} else if (strcmp(config.gravityspec, "SE") == 0) {
		*gravity = SouthEastGravity;
		*direction = UpWards;
	} else {
		errx(EXIT_FAILURE, "Unknown gravity %s", config.gravityspec);
	}
}

/* parse font string */
static void
parsefonts(struct Fonts *fnt, const char *s)
{
	const char *p;
	char buf[1024];
	size_t nfont = 0;

	fnt->nfonts = 1;
	for (p = s; *p; p++)
		if (*p == ',')
			fnt->nfonts++;

	if ((fnt->fonts = calloc(fnt->nfonts, sizeof *fnt->fonts)) == NULL)
		err(1, "calloc");

	p = s;
	while (*p != '\0') {
		size_t i;

		i = 0;
		while (isspace(*p))
			p++;
		while (i < sizeof buf && *p != '\0' && *p != ',')
			buf[i++] = *p++;
		if (i >= sizeof buf)
			errx(1, "font name too long");
		if (*p == ',')
			p++;
		buf[i] = '\0';
		if (nfont == 0)
			if ((fnt->pattern = FcNameParse((FcChar8 *)buf)) == NULL)
				errx(1, "the first font in the cache must be loaded from a font string");
		if ((fnt->fonts[nfont++] = XftFontOpenName(dpy, screen, buf)) == NULL)
			errx(1, "cannot load font");
	}
	fnt->texth = fnt->fonts[0]->height;
}

/* query monitor information */
static void
initmonitor(void)
{
	XineramaScreenInfo *info = NULL;
	int nmons;

	mon.x = mon.y = 0;
	mon.w = DisplayWidth(dpy, screen);
	mon.h = DisplayHeight(dpy, screen);
	if ((info = XineramaQueryScreens(dpy, &nmons)) != NULL) {
		int selmon;

		selmon = (mon.num >= 0 && mon.num < nmons) ? mon.num : 0;
		mon.x = info[selmon].x_org;
		mon.y = info[selmon].y_org;
		mon.w = info[selmon].width;
		mon.h = info[selmon].height;
		XFree(info);
	}
}

/* init draw context structure */
static void
initdc(void)
{
	/* get colors */
	ealloccolor(config.background_color,    &dc.background, 1);
	ealloccolor(config.foreground_color,    &dc.foreground, 1);
	ealloccolor(config.border_color,        &dc.border, 1);

	/* create common GC */
	dc.gc = XCreateGC(dpy, root, 0, NULL);

	/* try to get font */
	parsefonts(&titlefnt, config.titlefont);
	parsefonts(&bodyfnt, config.bodyfont);
}

/* Intern the used atoms */
static void
initatoms(void)
{
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeNotification] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMStateAbove] = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
}

/* watch ConfigureNotify on root window so we're notified when monitors change */
static void
initstructurenotify(void)
{
	XSelectInput(dpy, root, StructureNotifyMask);
}

/* allocate queue and set its members */
static struct Queue *
setqueue(void)
{
	struct Queue *queue;

	if ((queue = malloc(sizeof *queue)) == NULL)
		err(1, "malloc");

	queue->head = NULL;
	queue->tail = NULL;
	queue->change = 0;

	/* set geometry of notification queue */
	parsegravityspec(&queue->gravity, &queue->direction);
	parsegeometryspec(&queue->x, &queue->y, &queue->w, &queue->h);
	if (queue->w == 0)
		queue->w = DEFWIDTH;
	if (queue->h == 0)
		queue->h = titlefnt.texth + config.padding_pixels * 2;

	if (config.image_pixels <= 0)
		config.image_pixels = queue->h - config.padding_pixels;
	if (config.image_pixels < 0)
		config.image_pixels = 0;

	return queue;
}

/* get item of given window */
static struct Item *
getitem(struct Queue *queue, Window win)
{
	struct Item *item;

	for (item = queue->head; item; item = item->next)
		if (item->win == win)
			return item;
	return NULL;
}

/* copy area from item's pixmap into item's window */
static void
copypixmap(struct Item *item)
{
	XCopyArea(dpy, item->pixmap, item->win, dc.gc, 0, 0, item->w, item->h, 0, 0);
}

/* load and scale image */
static Imlib_Image
loadimage(const char *file)
{
	Imlib_Image image;
	Imlib_Load_Error errcode;
	const char *errstr;
	int width;
	int height;
	int imgsize;

	image = imlib_load_image_with_error_return(file, &errcode);
	if (*file == '\0') {
		warnx("could not load image (file name is blank)");
		return NULL;
	} else if (image == NULL) {
		switch (errcode) {
		case IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST:
			errstr = "file does not exist";
			break;
		case IMLIB_LOAD_ERROR_FILE_IS_DIRECTORY:
			errstr = "file is directory";
			break;
		case IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_READ:
		case IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE:
			errstr = "permission denied";
			break;
		case IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT:
			errstr = "unknown file format";
			break;
		case IMLIB_LOAD_ERROR_PATH_TOO_LONG:
			errstr = "path too long";
			break;
		case IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT:
		case IMLIB_LOAD_ERROR_PATH_COMPONENT_NOT_DIRECTORY:
		case IMLIB_LOAD_ERROR_PATH_POINTS_OUTSIDE_ADDRESS_SPACE:
			errstr = "improper path";
			break;
		case IMLIB_LOAD_ERROR_TOO_MANY_SYMBOLIC_LINKS:
			errstr = "too many symbolic links";
			break;
		case IMLIB_LOAD_ERROR_OUT_OF_MEMORY:
			errstr = "out of memory";
			break;
		case IMLIB_LOAD_ERROR_OUT_OF_FILE_DESCRIPTORS:
			errstr = "out of file descriptors";
			break;
		default:
			errstr = "unknown error";
			break;
		}
		warnx("could not load image (%s): %s", errstr, file);
		return NULL;
	}

	imlib_context_set_image(image);

	width = imlib_image_get_width();
	height = imlib_image_get_height();
	imgsize = MIN(width, height);

	image = imlib_create_cropped_scaled_image(0, 0, imgsize, imgsize,
	                                         config.image_pixels,
	                                         config.image_pixels);

	return image;
}

/* create window for item */
static void
createwindow(struct Item *item)
{
	XClassHint classhint = {"XNotify", "XNotify"};
	XSetWindowAttributes swa;

	swa.override_redirect = True;
	swa.background_pixel = dc.background.pixel;
	swa.border_pixel = dc.border.pixel;
	swa.save_under = True;  /* pop-up windows should save_under */
	swa.event_mask = ExposureMask | ButtonPressMask | PointerMotionMask;

	/* windows are created at 0,0 position for they'll be moved later */
	item->win = XCreateWindow(dpy, root, 0, 0, item->w, item->h, config.border_pixels,
	                          CopyFromParent, CopyFromParent, CopyFromParent,
	                          CWOverrideRedirect | CWBackPixel | CWBorderPixel |
	                          CWSaveUnder | CWEventMask, &swa);

	XSetClassHint(dpy, item->win, &classhint);

	XStoreName(dpy, item->win, "XNotify");
	XChangeProperty(dpy, item->win, netatom[NetWMName], utf8string, 8, PropModeReplace,
	                (unsigned char *)"XNotify", strlen("XNotify"));
	XChangeProperty(dpy, item->win, netatom[NetWMWindowType], XA_ATOM, 32, PropModeReplace,
	                (unsigned char *)&netatom[NetWMWindowTypeNotification], 1L);
	XChangeProperty(dpy, item->win, netatom[NetWMState], XA_ATOM, 32, PropModeReplace,
	                (unsigned char *)&netatom[NetWMStateAbove], 1L);
}

/* get next utf8 char from s return its codepoint and set next_ret to pointer to end of character */
static FcChar32
getnextutf8char(const char *s, const char **next_ret)
{
	static const unsigned char utfbyte[] = {0x80, 0x00, 0xC0, 0xE0, 0xF0};
	static const unsigned char utfmask[] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
	static const FcChar32 utfmin[] = {0, 0x00,  0x80,  0x800,  0x10000};
	static const FcChar32 utfmax[] = {0, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};
	/* 0xFFFD is the replacement character, used to represent unknown characters */
	static const FcChar32 unknown = 0xFFFD;
	FcChar32 ucode;         /* FcChar32 type holds 32 bits */
	size_t usize = 0;       /* n' of bytes of the utf8 character */
	size_t i;

	*next_ret = s+1;

	/* get code of first byte of utf8 character */
	for (i = 0; i < sizeof utfmask; i++) {
		if (((unsigned char)*s & utfmask[i]) == utfbyte[i]) {
			usize = i;
			ucode = (unsigned char)*s & ~utfmask[i];
			break;
		}
	}

	/* if first byte is a continuation byte or is not allowed, return unknown */
	if (i == sizeof utfmask || usize == 0)
		return unknown;

	/* check the other usize-1 bytes */
	s++;
	for (i = 1; i < usize; i++) {
		*next_ret = s+1;
		/* if byte is nul or is not a continuation byte, return unknown */
		if (*s == '\0' || ((unsigned char)*s & utfmask[0]) != utfbyte[0])
			return unknown;
		/* 6 is the number of relevant bits in the continuation byte */
		ucode = (ucode << 6) | ((unsigned char)*s & ~utfmask[0]);
		s++;
	}

	/* check if ucode is invalid or in utf-16 surrogate halves */
	if (!BETWEEN(ucode, utfmin[usize], utfmax[usize])
	    || BETWEEN (ucode, 0xD800, 0xDFFF))
		return unknown;

	return ucode;
}

/* get which font contains a given code point */
static XftFont *
getfontucode(struct Fonts *fnt, FcChar32 ucode)
{
	FcCharSet *fccharset = NULL;
	FcPattern *fcpattern = NULL;
	FcPattern *match = NULL;
	XftFont *retfont = NULL;
	XftResult result;
	size_t i;

	for (i = 0; i < fnt->nfonts; i++)
		if (XftCharExists(dpy, fnt->fonts[i], ucode) == FcTrue)
			return fnt->fonts[i];

	/* create a charset containing our code point */
	fccharset = FcCharSetCreate();
	FcCharSetAddChar(fccharset, ucode);

	/* create a pattern akin to the fnt->pattern but containing our charset */
	if (fccharset) {
		fcpattern = FcPatternDuplicate(fnt->pattern);
		FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	}

	/* find pattern matching fcpattern */
	if (fcpattern) {
		FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
		FcDefaultSubstitute(fcpattern);
		match = XftFontMatch(dpy, screen, fcpattern, &result);
	}

	/* if found a pattern, open its font */
	if (match) {
		retfont = XftFontOpenPattern(dpy, match);
		if (retfont && XftCharExists(dpy, retfont, ucode) == FcTrue) {
			if ((fnt->fonts = realloc(fnt->fonts, fnt->nfonts+1)) == NULL)
				err(1, "realloc");
			fnt->fonts[fnt->nfonts] = retfont;
			return fnt->fonts[fnt->nfonts++];
		} else {
			XftFontClose(dpy, retfont);
		}
	}

	/* in case no fount was found, return the first one */
	return fnt->fonts[0];
}

/* draw text into draw (if draw != NULL); return width of text glyphs */
static int
drawtext(struct Fonts *fnt, XftDraw *draw, XftColor *color, int x, int y, const char *text)
{
	int textwidth = 0;

	while (*text) {
		XftFont *currfont;
		XGlyphInfo ext;
		FcChar32 ucode;
		const char *next;
		size_t len;

		ucode = getnextutf8char(text, &next);
		currfont = getfontucode(fnt, ucode);

		len = next - text;
		XftTextExtentsUtf8(dpy, currfont, (XftChar8 *)text, len, &ext);
		textwidth += ext.xOff;

		if (draw) {
			int texty;

			texty = y + (fnt->texth - (currfont->ascent + currfont->descent))/2 + currfont->ascent;
			XftDrawStringUtf8(draw, color, currfont, x, texty, (XftChar8 *)text, len);
			x += ext.xOff;
		}

		text = next;
	}

	return textwidth;
}

/* draw contents of notification item on item->pixmap */
static void
drawitem(struct Item *item)
{
	XftDraw *draw;
	int x, y;
	int xaligned, oldx;

	x = config.padding_pixels;
	y = config.padding_pixels;
	item->pixmap = XCreatePixmap(dpy, item->win, item->w, item->h, depth);
	draw = XftDrawCreate(dpy, item->pixmap, visual, colormap);

	/* draw background */
	XSetForeground(dpy, dc.gc, item->background.pixel);
	XFillRectangle(dpy, item->pixmap, dc.gc, 0, 0, item->w, item->h);

	/* draw image */
	if (item->image) {
		imlib_context_set_image(item->image);
		imlib_context_set_drawable(item->pixmap);
		imlib_render_image_on_drawable(x / 2, y / 2);
		imlib_free_image();
		x += config.image_pixels;
	}

	/* draw text */
	oldx = x;
	switch (config.alignment) {
	case LeftAlignment:
		break;
	case CenterAlignment:
		xaligned = drawtext(&titlefnt, NULL, NULL, 0, 0, item->title);
		xaligned = x + (item->w - x - titlefnt.texth - xaligned) / 2;
		x = MAX(x, xaligned);
		break;
	case RightAlignment:
		xaligned = drawtext(&titlefnt, NULL, NULL, 0, 0, item->title);
		xaligned = x + (item->w - x - titlefnt.texth - xaligned);
		x = MAX(x, xaligned);
		break;
	}
	if (item->body)
		y = (item->h - config.leading_pixels) / 2 - titlefnt.texth;
	else
		y = (item->h - titlefnt.texth) / 2;
	drawtext(&titlefnt, draw, &item->foreground, x, y, item->title);

	/* draw text body */
	if (item->body) {
		y += titlefnt.texth + config.leading_pixels;
		x = oldx;
		switch (config.alignment) {
		case LeftAlignment:
			break;
		case CenterAlignment:
			xaligned = drawtext(&bodyfnt, NULL, NULL, 0, 0, item->body);
			xaligned = x + (item->w - x - bodyfnt.texth - xaligned) / 2;
			x = MAX(x, xaligned);
			break;
		case RightAlignment:
			xaligned = drawtext(&bodyfnt, NULL, NULL, 0, 0, item->body);
			xaligned = x + (item->w - x - bodyfnt.texth - xaligned);
			x = MAX(x, xaligned);
			break;
		}
		drawtext(&bodyfnt, draw, &item->foreground, x, y, item->body);
	}

	/* change border color */
	XSetWindowBorder(dpy, item->win, item->border.pixel);

	XftDrawDestroy(draw);
}

/* reset time of item */
static void
resettime(struct Item *item)
{
	item->time = time(NULL);
}

/* add item notification item and set its window and contents */
static void
additem(struct Queue *queue, const char *title, const char *body, const char *file, const char *background, const char *foreground, const char *border)
{
	struct Item *item;
	int titlew, bodyw;
	int w, h;

	if ((item = malloc(sizeof *item)) == NULL)
		err(1, "malloc");
	item->next = NULL;
	item->title = strdup(title);
	item->body = (body) ? strdup(body) : NULL;
	item->image = (file) ? loadimage(file) : NULL;
	if (!queue->head)
		queue->head = item;
	else
		queue->tail->next = item;
	item->prev = queue->tail;
	queue->tail = item;

	/* allocate colors */
	if (!background || ealloccolor(background, &item->background, 0) == -1)
		item->background = dc.background;
	if (!foreground || ealloccolor(foreground, &item->foreground, 0) == -1)
		item->foreground = dc.foreground;
	if (!border || ealloccolor(border, &item->border, 0) == -1)
		item->border = dc.border;

	/* compute notification height */
	if (item->body)
		h = titlefnt.texth + bodyfnt.texth + config.padding_pixels + config.leading_pixels;
	else
		h = titlefnt.texth + config.padding_pixels;
	
	h = config.padding_pixels + ((item->image) ? MAX(h, config.image_pixels) : h);
	item->h = MAX(h, queue->h);

	/* compute notification width */
	if (config.shrink) {
		titlew = drawtext(&titlefnt, NULL, NULL, 0, 0, item->title);
		bodyw = (item->body) ? drawtext(&bodyfnt, NULL, NULL, 0, 0, item->body) : 0;
		w = MAX(titlew, bodyw);
		if (item->image) {
			w += config.image_pixels + config.padding_pixels * 2;
		} else {
			w += config.padding_pixels * 2;
		}
		item->w = MIN(w, queue->w);
	} else {
		item->w = queue->w;
	}

	/* call functions that set the item */
	createwindow(item);
	resettime(item);
	drawitem(item);

	/* a new item was added to the queue, so the queue changed */
	queue->change = 1;
}

/* delete item */
static void
delitem(struct Queue *queue, struct Item *item)
{
	free(item->title);
	if (item->body)
		free(item->body);
	XFreePixmap(dpy, item->pixmap);
	XDestroyWindow(dpy, item->win);
	if (item->prev)
		item->prev->next = item->next;
	else
		queue->head = item->next;
	if (item->next)
		item->next->prev = item->prev;
	else
		queue->tail = item->prev;
	free(item);
	queue->change = 1;
}

/* check the type of option given to a notification item */
static enum ItemOption
optiontype(const char *s)
{
	if (strncmp(s, "IMG:", 4) == 0)
		return IMG;
	if (strncmp(s, "BG:", 3) == 0)
		return BG;
	if (strncmp(s, "FG:", 3) == 0)
		return FG;
	if (strncmp(s, "BRD:", 4) == 0)
		return BRD;
	return UNKNOWN;
}

/* destroy all notification items */
static void
cleanitems(struct Queue *queue)
{
	struct Item *item;
	struct Item *tmp;

	item = queue->head;
	while (item) {
		tmp = item;
		item = item->next;
		delitem(queue, tmp);
	}
}

/* read stdin */
static void
parseinput(struct Queue *queue, char *s)
{
	enum ItemOption option;
	char *title, *body, *file, *fg, *bg, *brd;

	/* get the title */
	title = strtok(s, "\t\n");

	/* get the filename */
	file = NULL;
	fg = bg = brd = NULL;
	while (title && (option = optiontype(title)) != UNKNOWN) {
		switch (option) {
		case IMG:
			file = title + 4;
			title = strtok(NULL, "\t\n");
			break;
		case BG:
			bg = title + 3;
			title = strtok(NULL, "\t\n");
			break;
		case FG:
			fg = title + 3;
			title = strtok(NULL, "\t\n");
			break;
		case BRD:
			brd = title + 4;
			title = strtok(NULL, "\t\n");
			break;
		default:
			break;
		}
	}

	/* get the body */
	body = strtok(NULL, "\n");
	if (body)
		while (*body == '\t')
			body++;

	if (!title)
		return;

	if (oflag)
		cleanitems(queue);

	additem(queue, title, body, file, bg, fg, brd);
}

/* read x events */
static void
readevent(struct Queue *queue)
{
	struct Item *item;
	XEvent ev;

	while (XPending(dpy) && !XNextEvent(dpy, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0 && (item = getitem(queue, ev.xexpose.window)) != NULL)
				copypixmap(item);
			break;
		case ButtonPress:
			if ((item = getitem(queue, ev.xexpose.window)) != NULL)
				delitem(queue, item);
			break;
		case MotionNotify:
			if ((item = getitem(queue, ev.xmotion.window)) != NULL)
				resettime(item);
			break;
		case ConfigureNotify:   /* monitor arrangement changed */
			if (ev.xproperty.window == root) {
				initmonitor();
				queue->change = 1;
			}
			break;
		}
	}
}

/* check whether items have passed the time */
static void
timeitems(struct Queue *queue)
{
	struct Item *item;
	struct Item *tmp;

	item = queue->head;
	while (item) {
		tmp = item;
		item = item->next;
		if (time(NULL) - tmp->time > config.sec) {
			delitem(queue, tmp);
		}
	}
}

static void
moveitems(struct Queue *queue)
{
	struct Item *item;
	int x, y;
	int h = 0;

	for (item = queue->head; item; item = item->next) {
		x = queue->x + mon.x;
		y = queue->y + mon.y;
		switch (queue->gravity) {
		case NorthWestGravity:
			break;
		case NorthGravity:
			x += (mon.w - item->w) / 2 - config.border_pixels;
			break;
		case NorthEastGravity:
			x += mon.w - item->w - config.border_pixels * 2;
			break;
		case WestGravity:
			y += (mon.h - item->h) / 2 - config.border_pixels;
			break;
		case CenterGravity:
			x += (mon.w - item->w) / 2 - config.border_pixels;
			y += (mon.h - item->h) / 2 - config.border_pixels;
			break;
		case EastGravity:
			x += mon.w - item->w - config.border_pixels * 2;
			y += (mon.h - item->h) / 2 - config.border_pixels;
			break;
		case SouthWestGravity:
			y += mon.h - item->h - config.border_pixels * 2;
			break;
		case SouthGravity:
			x += (mon.w - item->w) / 2 - config.border_pixels;
			y += mon.h - item->h - config.border_pixels * 2;
			break;
		case SouthEastGravity:
			x += mon.w - item->w - config.border_pixels * 2;
			y += mon.h - item->h - config.border_pixels * 2;
			break;
		}

		if (queue->direction == DownWards)
			y += h;
		else
			y -= h;
		h += item->h + config.gap_pixels + config.border_pixels * 2;

		XMoveWindow(dpy, item->win, x, y);
		XMapWindow(dpy, item->win);
		copypixmap(item);
	}

	queue->change = 0;
}

/* clean up dc elements */
static void
cleandc(void)
{
	XftColorFree(dpy, visual, colormap, &dc.background);
	XftColorFree(dpy, visual, colormap, &dc.foreground);
	XftColorFree(dpy, visual, colormap, &dc.border);

	XFreeColormap(dpy, colormap);

	XFreeGC(dpy, dc.gc);
}

/* xnotify: show notifications from stdin */
int
main(int argc, char *argv[])
{
	struct Queue *queue;    /* it contains the queue of notifications and their geometry */
	struct pollfd pfd[2];   /* [2] for stdin and xfd, see poll(2) */
	char buf[BUFSIZ];       /* buffer for stdin */
	int timeout = -1;       /* maximum interval for poll(2) to complete */
	int flags;              /* status flags for stdin */
	int done = 0;           /* set to 1 when stdin reaches EOF */

	/* open connection to server and set X variables */
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	visual = DefaultVisual(dpy, screen);
	depth = DefaultDepth(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	xfd = XConnectionNumber(dpy);
	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);

	/* imlib2 stuff */
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(visual);
	imlib_context_set_colormap(colormap);

	/* get configuration */
	getresources();
	getoptions(argc, argv);

	/* init stuff */
	initmonitor();
	initdc();
	initatoms();
	initstructurenotify();

	/* set up queue of notifications */
	queue = setqueue();

	/* Make stdin nonblocking */
	if ((flags = fcntl(STDIN_FILENO, F_GETFL)) == -1)
		err(1, "could not get status flags for stdin");
	if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1)
		err(1, "could not set status flags for stdin");

	/* prepare the structure for poll(2) */
	pfd[0].fd = STDIN_FILENO;
	pfd[1].fd = xfd;
	pfd[0].events = pfd[1].events = POLLIN;

	/* run main loop */
	for (;;) {
		if (poll(pfd, 2, timeout) > 0) {
			if (pfd[0].revents & POLLHUP) {
				pfd[0].fd = -1;
				done = 1;
			}
			if (pfd[0].revents & POLLIN) {
				if (fgets(buf, sizeof buf, stdin) == NULL)
					break;
				parseinput(queue, buf);
			}
			if (pfd[1].revents & POLLIN) {
				readevent(queue);
			}
		}
		timeitems(queue);
		if (queue->change)
			moveitems(queue);
		timeout = (queue->head) ? 1000 : -1;
		XFlush(dpy);

		if (done && !queue->head)
			break;
	}

	/* clean up stuff */
	cleanitems(queue);
	cleandc();
	free(queue);

	/* close connection to server */
	XrmDestroyDatabase(xdb);
	XCloseDisplay(dpy);

	return 0;
}
