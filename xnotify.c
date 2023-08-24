#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>
#include <Imlib2.h>

#include "ctrlfnt.h"

#define APP_CLASS           "XNotify"
#define APP_NAME            "xnotify"
#define DEFWIDTH            350     /* default width of a notification */
#define MAXLINES            128     /* maximum number of unwrapped lines */
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))
#define RED(v)   ((((v) & 0xFF0000) >> 8) | (((v) & 0xFF0000) >> 16))
#define GREEN(v) ((((v) & 0x00FF00)     ) | (((v) & 0x00FF00) >> 8))
#define BLUE(v)  ((((v) & 0x0000FF) << 8) | (((v) & 0x0000FF)     ))

#define ATOMS                                   \
	X(UTF8_STRING)                          \
	X(_NET_WM_NAME)                         \
	X(_NET_WM_WINDOW_TYPE)                  \
	X(_NET_WM_WINDOW_TYPE_NOTIFICATION)     \
	X(_NET_WM_STATE)                        \
	X(_NET_WM_STATE_ABOVE)

#define RESOURCES                                                                        \
	/* ENUM            CLASS                 NAME                   DEFAULT        */\
	X(RES_ALIGNMENT,  "Alignment",          "alignment",            LeftAlignment   )\
	X(RES_BACKGROUND, "Background",         "background",           0x000000        )\
	X(RES_BORDERCLR,  "BorderColor",        "borderColor",          0x3465A4        )\
	X(RES_BORDERWID,  "BorderWidth",        "borderWidth",          1               )\
	X(RES_FACENAME,   "FaceName",           "faceName",             0               )\
	X(RES_FOREGROUND, "Foreground",         "foreground",           0xFFFFFF        )\
	X(RES_GAP,        "Gap",                "gap",                  7               )\
	X(RES_GRAVITY,    "Gravity",            "gravity",              NorthEastGravity)\
	X(RES_IMAGEWID,   "ImageWidth",         "imageWidth",           80              )\
	X(RES_LEADING,    "Leading",            "leading",              5               )\
	X(RES_MAXHEIGHT,  "MaxHeight",          "maxHeight",            300             )\
	X(RES_OPACITY,    "Opacity",            "opacity",              0xFFFF          )\
	X(RES_PADDING,    "Padding",            "padding",              10              )\
	X(RES_SHRINK,     "Shrink",             "shrink",               0               )\

enum ItemOption {IMG, BG, FG, BRD, TAG, CMD, SEC, BAR, UNKNOWN};

enum {DownWards, UpWards};

enum {LeftAlignment, CenterAlignment, RightAlignment};

enum {
	SIGNAL_NONE    = 0,
	SIGNAL_KILL    = 1,
	SIGNAL_CMD     = 2,
	SIGNAL_KILLALL = 3,
};

enum Atom {
#define X(atom) atom,
	ATOMS
	NATOMS
#undef  X
};

enum Resource {
#define X(resource, class, name, value) resource,
	RESOURCES
	NRESOURCES
#undef  X
};

typedef struct {
	XrmClass        class;
	XrmName         name;
} Resource;

/* monitor geometry structure */
struct Monitor {
	int num;
	int x, y, w, h;
};

/* notification item specification structure */
struct Itemspec {
	char *firstline;
	char *otherlines;
	char *file;
	char *background;
	char *foreground;
	char *border;
	char *tag;
	char *cmd;
	int bar;
	int sec;
};

/* notification item structure */
struct Item {
	struct Item *prev, *next;

	int nlines;
	char *line[MAXLINES];
	char *tag;
	char *cmd;

	time_t time;
	int sec;

	int w, h;
	int imgw;
	int textw;

	int bar;

	XRenderColor background;
	XRenderColor foreground;
	XRenderColor borderclr;

	Imlib_Image image;
	Window win;
};

/* notification queue structure */
struct Queue {
	/* queue pointers */
	struct Item *head, *tail;

	/* general geometry for the notification queue */
	int x, y;       /* position of the first notification */
	int w, h;       /* width and height of individual notifications */

	/* whether the queue changed */
	bool change;
};

/* ellipsis size and font structure */
struct Ellipsis {
	char *s;
	size_t len;     /* length of s */
	int width;      /* size of ellipsis string */
};

/* X stuff */
static int alignment;
static int seconds = 10;
static int saveargc;
static char **saveargv;
static Display *dpy;
static Colormap colormap;
static Visual *visual;
static Window root;
static int screen;
static int depth;
static int xfd;
static struct Monitor mon;
static Atom atoms[NATOMS];
static Resource application, resources[NRESOURCES];
static XRenderColor background, foreground, borderclr;
static struct Ellipsis ellipsis;
static CtrlFontSet *fontset = NULL;
static int fonth;
static XRenderPictFormat *xformat, *alphaformat;
static int gravity;    /* NorthEastGravity, NorthGravity, etc */
static int direction;  /* DownWards or UpWards */
static int gap_pixels, border_pixels, leading_pixels, padding_pixels;
static int max_height, image_pixels;
static unsigned short opacity;
static bool shrink;
static unsigned int actionbutton = Button3;

/* flags */
static bool oflag;      /* whether only one notification must exist at a time */
static bool wflag;      /* whether to let window manager manage notifications */
static bool rflag;      /* whether to watch for notifications in the root window */
volatile sig_atomic_t sigflag;

void
usage(void)
{
	(void)fprintf(stderr, "usage: xnotify [-ow] [-G gravity] [-b button] [-g geometry]\n");
	(void)fprintf(stderr, "               [-h height] [-m monitor] [-s seconds]\n");
	exit(1);
}

static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

static char *
gettextprop(Window window, Atom prop)
{
	char *text;
	unsigned char *p;
	unsigned long len;
	unsigned long dl;               /* dummy variable */
	int format, status;
	Atom type;

	text = NULL;
	status = XGetWindowProperty(
		dpy,
		window,
		prop,
		0L,
		0x1FFFFFFF,
		False,
		AnyPropertyType,
		&type, &format,
		&len, &dl,
		&p
	);
	if (status != Success || len == 0 || p == NULL) {
		goto done;
	}
	if ((text = emalloc(len + 1)) == NULL) {
		goto done;
	}
	memcpy(text, p, len);
	text[len] = '\0';
done:
	XFree(p);
	return text;
}

static void
parsegravityspec(int *gravity, int *direction, const char *gravityspec)
{
	if (gravityspec == NULL || strcmp(gravityspec, "N") == 0) {
		*gravity = NorthGravity;
		*direction = DownWards;
	} else if (strcmp(gravityspec, "NW") == 0) {
		*gravity = NorthWestGravity;
		*direction = DownWards;
	} else if (strcmp(gravityspec, "NE") == 0) {
		*gravity = NorthEastGravity;
		*direction = DownWards;
	} else if (strcmp(gravityspec, "W") == 0) {
		*gravity = WestGravity;
		*direction = DownWards;
	} else if (strcmp(gravityspec, "C") == 0) {
		*gravity = CenterGravity;
		*direction = DownWards;
	} else if (strcmp(gravityspec, "E") == 0) {
		*gravity = EastGravity;
		*direction = DownWards;
	} else if (strcmp(gravityspec, "SW") == 0) {
		*gravity = SouthWestGravity;
		*direction = UpWards;
	} else if (strcmp(gravityspec, "S") == 0) {
		*gravity = SouthGravity;
		*direction = UpWards;
	} else if (strcmp(gravityspec, "SE") == 0) {
		*gravity = SouthEastGravity;
		*direction = UpWards;
	} else {
		warnx("Unknown gravity %s", gravityspec);
	}
}

static void
parseoptions(int argc, char *argv[])
{
	unsigned long n;
	int ch;

	while ((ch = getopt(argc, argv, "G:b:g:h:m:ors:w")) != -1) {
		switch (ch) {
		case 'G':
			parsegravityspec(&gravity, &direction, optarg);
			break;
		case 'b':
			if (*(optarg+1) != '\0')
				break;
			switch (*optarg) {
			case '1':
				actionbutton = Button1;
				break;
			case '2':
				actionbutton = Button2;
				break;
			case '3':
				actionbutton = Button3;
				break;
			default:
				break;
			}
			break;
		case 'g':
			//config.geometryspec = optarg;
			break;
		case 'h':
			if ((n = strtoul(optarg, NULL, 10)) < INT_MAX)
				max_height = n;
			break;
		case 'm':
			mon.num = atoi(optarg);
			break;
		case 'o':
			oflag = true;
			break;
		case 'r':
			rflag = true;
			break;
		case 's':
			if ((n = strtoul(optarg, NULL, 10)) < INT_MAX)
				seconds = n;
			break;
		case 'w':
			wflag = 1;
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

static void
setcolor(XRenderColor *color, const char *value)
{
	XColor xcolor;

	if (value == NULL)
		return;
	if (!XParseColor(dpy, colormap, value, &xcolor)) {
		warnx("%s: unknown color name", value);
		return;
	}
	color->red   = (xcolor.flags & DoRed)   ? xcolor.red   : 0x0000;
	color->green = (xcolor.flags & DoGreen) ? xcolor.green : 0x0000;
	color->blue  = (xcolor.flags & DoBlue)  ? xcolor.blue  : 0x0000;
	color->alpha = 0xFFFF;
}

static int
getnum(const char **s, int *n)
{
	int retval;
	long num;
	char *endp;

	num = strtol(*s, &endp, 10);
	retval = (num > INT_MAX || num < 0 || endp == *s);
	*s = endp;
	*n = num;
	return retval;
}

static void
sigusr1handler(int sig)
{
	(void)sig;
	sigflag = SIGNAL_KILL;
}

static void
sigusr2handler(int sig)
{
	(void)sig;
	sigflag = SIGNAL_CMD;
}

static void
sighuphandler(int sig)
{
	(void)sig;
	sigflag = SIGNAL_KILLALL;
}

static void
initsignal(void)
{
	struct sigaction sa;

	sa.sa_handler = sigusr1handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) == -1)
		err(1, "sigaction");

	sa.sa_handler = sigusr2handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR2, &sa, NULL) == -1)
		err(1, "sigaction");

	sa.sa_handler = sighuphandler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGHUP, &sa, NULL) == -1)
		err(1, "sigaction");
}

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

static struct Queue *
setqueue(void)
{
	struct Queue *queue;
	int minw;

	if ((queue = malloc(sizeof *queue)) == NULL)
		err(1, "malloc");

	queue->head = NULL;
	queue->tail = NULL;
	queue->change = false;

	/* set geometry of notification queue */
	queue->x = 0;
	queue->y = 0;
	queue->w = 0;
	queue->h = 0;
	//parsegeometryspec(&queue->x, &queue->y, &queue->w, &queue->h);
	minw = ellipsis.width + image_pixels + padding_pixels * 3 + 1;
	if (queue->w < minw)
		queue->w = MAX(DEFWIDTH, minw);

	return queue;
}

static struct Item *
getitem(struct Queue *queue, Window win)
{
	struct Item *item;

	for (item = queue->head; item; item = item->next)
		if (item->win == win)
			return item;
	return NULL;
}

static Imlib_Image
loadimage(const char *file)
{
	Imlib_Image image;
	Imlib_Load_Error errcode;
	const char *errstr;

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
	imlib_image_set_changes_on_disk();
	return image;
}

static void
createwindow(struct Item *item)
{
	item->win = XCreateWindow(
		dpy, root,
		0, 0,           /* placed at (0,0) for now; will be moved later */
		item->w,
		1,              /* sized with 1px for now; will be resized later */
		0,
		depth,
		InputOutput,
		visual,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel |
		CWSaveUnder | CWEventMask | CWColormap,
		&(XSetWindowAttributes){
			.override_redirect = wflag ? False : True,
			.background_pixel = 0,
			.border_pixel =0,
			.colormap = colormap,
			.save_under = True,
			.event_mask = ButtonPressMask | PointerMotionMask,
		}
	);
	XmbSetWMProperties(
		dpy, item->win,
		APP_CLASS,      /* title name */
		APP_CLASS,      /* icon name */
		saveargv,
		saveargc,
		NULL,
		NULL,
		&(XClassHint){
			.res_class = APP_CLASS,
			.res_name = APP_NAME,
		}
	);
	XChangeProperty(
		dpy,
		item->win,
		atoms[_NET_WM_NAME],
		atoms[UTF8_STRING],
		8,
		PropModeReplace,
		(unsigned char *)APP_CLASS,
		strlen(APP_CLASS)
	);
	XChangeProperty(
		dpy,
		item->win,
		atoms[_NET_WM_WINDOW_TYPE],
		XA_ATOM,
		32,
		PropModeReplace,
		(unsigned char *)&atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION],
		1
	);
	XChangeProperty(
		dpy,
		item->win,
		atoms[_NET_WM_STATE],
		XA_ATOM,
		32,
		PropModeReplace,
		(unsigned char *)&atoms[_NET_WM_STATE_ABOVE],
		1
	);
}

static void
drawitem(struct Item *item)
{
	Pixmap pixmap, fg, alpha;
	Picture picture;
	Imlib_Image image = NULL;
	const char *text;
	size_t len, j;
	int xaligned;
	int bar, i, x, y, newh;
	int texth, imgh, imgw;
	int origimgw, origimgh;

	pixmap = XCreatePixmap(dpy, item->win, item->w, max_height, depth);
	picture = XRenderCreatePicture(dpy, pixmap, xformat, 0, NULL);
	XRenderFillRectangle(
		dpy,
		PictOpSrc,
		picture,
		&item->background,
		0, 0,
		item->w,
		max_height
	);

	/* draw opacity */
	alpha = XRenderCreateSolidFill(dpy, &(XRenderColor){
		.red = 0, .green = 0, .blue = 0,
		.alpha = opacity
	});
	XRenderComposite(
		dpy,
		PictOpSrc,
		picture,
		alpha,
		picture,
		0, 0,
		0, 0,
		0, 0,
		item->w,
		max_height
	);

	/* draw image */
	y = padding_pixels;
	fg = XRenderCreateSolidFill(dpy, &item->foreground);
	imgw = imgh = 0;
	if (item->image && item->imgw > 0) {
		imlib_context_set_image(item->image);
		imlib_context_set_drawable(pixmap);
		origimgw = imlib_image_get_width();
		origimgh = imlib_image_get_height();
		if (imgw > imgh) {
			imgw = item->imgw;
			imgh = (origimgh * item->imgw) / origimgw;
		} else {
			imgw = (origimgw * item->imgw) / origimgh;
			imgh = item->imgw;
		}
		image = imlib_create_cropped_scaled_image(0, 0, origimgw, origimgh, imgw, imgh);
		imlib_free_image();
		imlib_context_set_image(image);
		imlib_render_image_on_drawable(
			padding_pixels + (item->imgw - imgw) / 2,
			padding_pixels + (item->imgw - imgh) / 2
		);
		imlib_free_image();
	}

	/* draw text */
	texth = 0;
	for (i = 0; item->textw > 0 && i < item->nlines; i++) {
		text = item->line[i];
		x = padding_pixels;
		x += (image && item->imgw > 0 ? item->imgw + padding_pixels : 0);
		while (texth <= max_height) {
			for (len = j = 0; text[len] != '\0'; len = j, j += strcspn(text + j, " \t")) {
				j += strspn(text + j, " \t");
				if (ctrlfnt_width(fontset, text, j) > item->textw)
					break;
			}
			if (len < 1)
				break;
			if (xaligned <= 0)
				break;
			switch (alignment) {
			case LeftAlignment:
				break;
			case CenterAlignment:
				xaligned = (item->textw - xaligned) / 2;
				x = MAX(x, xaligned);
				break;
			case RightAlignment:
				xaligned = item->textw - xaligned;
				x = MAX(x, xaligned);
				break;
			default:
				break;
			}
			ctrlfnt_draw(
				fontset,
				picture,
				fg,
				(XRectangle){
					.x = x,
					.y = y + texth,
					.width = item->textw,
					.height = fonth,
				},
				text, len
			);
			texth += fonth + leading_pixels;
			text += len;
		}
	}
	if (texth > leading_pixels)
		texth -= leading_pixels;

	x = padding_pixels;
	x += (image && item->imgw > 0 ? item->imgw + padding_pixels : 0);
	/* draw bar */
	if (item->bar > 0) {
		bar = (item->textw * item->bar) / 100;
		bar = MIN(bar, item->textw);
		XRenderFillRectangle(
			dpy,
			PictOpSrc,
			picture,
			&item->foreground,
			x, y + texth,
			bar,
			fonth
		);

		texth += fonth;
	}

	/* resize notification window based on its contents */
	newh = MAX(imgh, texth) + 2 * padding_pixels;
	item->h = MAX(item->h, newh);
	XResizeWindow(dpy, item->win, item->w, item->h);

	/* change border color */
	XSetWindowBackgroundPixmap(dpy, item->win, pixmap);
	XClearWindow(dpy, item->win);
	XFreePixmap(dpy, pixmap);
	XRenderFreePicture(dpy, picture);
	XRenderFreePicture(dpy, fg);
	XRenderFreePicture(dpy, alpha);
}

static void
resettime(struct Item *item)
{
	item->time = time(NULL);
}

static char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

static void
additem(struct Queue *queue, struct Itemspec *itemspec)
{
	const char *text;
	struct Item *item;
	int w, i;

	if ((item = malloc(sizeof *item)) == NULL)
		err(1, "malloc");
	item->next = NULL;
	item->image = (itemspec->file) ? loadimage(itemspec->file) : NULL;
	item->tag = (itemspec->tag) ? estrdup(itemspec->tag) : NULL;
	item->cmd = (itemspec->cmd) ? estrdup(itemspec->cmd) : NULL;
	item->sec = itemspec->sec;
	item->bar = itemspec->bar;
	if (!queue->head)
		queue->head = item;
	else
		queue->tail->next = item;
	item->prev = queue->tail;
	queue->tail = item;

	/* allocate texts */
	item->line[0] = (itemspec->firstline) ? estrdup(itemspec->firstline) : NULL;
	text = strtok(itemspec->otherlines, "\t\n");
	for (i = 1; i < MAXLINES && text != NULL; i++) {
		item->line[i] = estrdup(text);
		text = strtok(NULL, "\t\n");
	}
	item->nlines = i;

	/* allocate colors */
	item->background = background;
	item->foreground = foreground;
	item->borderclr = borderclr;
	setcolor(&item->background, itemspec->background);
	setcolor(&item->foreground, itemspec->foreground);
	setcolor(&item->borderclr, itemspec->border);

	/* compute notification width and height */
	item->imgw = image_pixels;
	item->h = queue->h;
	for (i = 0; i < item->nlines; i++) {
		text = item->line[i];
		w = ctrlfnt_width(fontset, text, strlen(text));
	}
	if (shrink) {
		if (item->image) {
			item->textw = queue->w - image_pixels - padding_pixels * 3;
			item->textw = MIN(w, item->textw);
			item->w = item->textw + image_pixels + padding_pixels * 3;
		} else {
			item->textw = queue->w - padding_pixels * 2;
			item->textw = MIN(w, item->textw);
			item->w = item->textw + padding_pixels * 2;
		}
	} else {
		item->w = queue->w;
		if (item->image) {
			item->textw = queue->w - image_pixels - padding_pixels * (2 + (item->line[0] ? 1 : 0));
			if (!image_pixels) {
				item->textw = MIN(item->textw, w);
			}
			item->imgw = queue->w - item->textw - padding_pixels * (2 + (item->line[0] ? 1 : 0));
		} else {
			item->textw = queue->w - padding_pixels * 2;
		}
	}

	/* call functions that set the item */
	createwindow(item);
	resettime(item);
	drawitem(item);

	/* a new item was added to the queue, so the queue changed */
	queue->change = true;
}

static void
delitem(struct Queue *queue, struct Item *item)
{
	int i;

	for (i = 0; i < item->nlines; i++)
		free(item->line[i]);
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
	queue->change = true;
}

static void
cmditem(struct Item *item)
{
	if (item->cmd != NULL)
		printf("%s\n", item->cmd);
	fflush(stdout);
}

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
	if (strncmp(s, "TAG:", 4) == 0)
		return TAG;
	if (strncmp(s, "CMD:", 4) == 0)
		return CMD;
	if (strncmp(s, "SEC:", 4) == 0)
		return SEC;
	if (strncmp(s, "BAR:", 4) == 0)
		return BAR;
	return UNKNOWN;
}

static struct Itemspec *
parseline(char *s)
{
	enum ItemOption option;
	struct Itemspec *itemspec;
	const char *t;
	int n;

	if ((itemspec = malloc(sizeof *itemspec)) == NULL)
		err(1, "malloc");

	/* get the filename */
	itemspec->file = NULL;
	itemspec->foreground = NULL;
	itemspec->background = NULL;
	itemspec->border = NULL;
	itemspec->tag = NULL;
	itemspec->cmd = NULL;
	itemspec->bar = -1;
	itemspec->sec = seconds;
	itemspec->firstline = strtok(s, "\t\n");
	while (itemspec->firstline && (option = optiontype(itemspec->firstline)) != UNKNOWN) {
		switch (option) {
		case IMG:
			itemspec->file = itemspec->firstline + 4;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case BG:
			itemspec->background = itemspec->firstline + 3;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case FG:
			itemspec->foreground = itemspec->firstline + 3;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case BRD:
			itemspec->border = itemspec->firstline + 4;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case TAG:
			itemspec->tag = itemspec->firstline + 4;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case CMD:
			itemspec->cmd = itemspec->firstline + 4;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case BAR:
			t = itemspec->firstline + 4;
			if (!getnum(&t, &n) && n >= 0 && n <= 100)
				itemspec->bar = n;
			else
				itemspec->bar = -1;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		case SEC:
			t = itemspec->firstline + 4;
			if (!getnum(&t, &n))
				itemspec->sec = n;
			itemspec->firstline = strtok(NULL, "\t\n");
			break;
		default:
			break;
		}
	}

	/* get the body */
	itemspec->otherlines = strtok(NULL, "\n");
	if (itemspec->otherlines)
		while (*itemspec->otherlines == '\t')
			itemspec->otherlines++;

	if (!itemspec->firstline && !itemspec->file)
		return NULL;

	return itemspec;
}

static void
timeitems(struct Queue *queue)
{
	struct Item *item;
	struct Item *tmp;

	item = queue->head;
	while (item) {
		tmp = item;
		item = item->next;
		if (tmp->sec && time(NULL) - tmp->time >= tmp->sec) {
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

	/*
	 * A notification has been deleted or added;
	 * reorder the queue of notifications
	 */
	for (item = queue->head; item; item = item->next) {
		x = queue->x + mon.x;
		y = queue->y + mon.y;
		switch (gravity) {
		case NorthWestGravity:
			break;
		case NorthGravity:
			x += (mon.w - item->w) / 2 - border_pixels;
			break;
		case NorthEastGravity:
			x += mon.w - item->w - border_pixels * 2;
			break;
		case WestGravity:
			y += (mon.h - item->h) / 2 - border_pixels;
			break;
		case CenterGravity:
			x += (mon.w - item->w) / 2 - border_pixels;
			y += (mon.h - item->h) / 2 - border_pixels;
			break;
		case EastGravity:
			x += mon.w - item->w - border_pixels * 2;
			y += (mon.h - item->h) / 2 - border_pixels;
			break;
		case SouthWestGravity:
			y += mon.h - item->h - border_pixels * 2;
			break;
		case SouthGravity:
			x += (mon.w - item->w) / 2 - border_pixels;
			y += mon.h - item->h - border_pixels * 2;
			break;
		case SouthEastGravity:
			x += mon.w - item->w - border_pixels * 2;
			y += mon.h - item->h - border_pixels * 2;
			break;
		}

		if (direction == DownWards)
			y += h;
		else
			y -= h;
		h += item->h + gap_pixels + border_pixels * 2;
		XMoveWindow(dpy, item->win, x, y);
		XMapWindow(dpy, item->win);
	}

	queue->change = false;
}

static void
cleanitems(struct Queue *queue, const char *tag)
{
	struct Item *item;
	struct Item *tmp;

	/*
	 * Free all notification items of the given tag;
	 * or free all items if tag is NULL
	 */
	item = queue->head;
	while (item) {
		tmp = item;
		item = item->next;
		if (tag == NULL || (tmp->tag && strcmp(tmp->tag, tag) == 0)) {
			delitem(queue, tmp);
		}
	}
}

static char *
getresource(XrmDatabase xdb, enum Resource res)
{
	XrmRepresentation tmp;
	XrmValue xval;

	return XrmQGetResource(
		xdb,
		(XrmName[]){
			application.name,
			resources[res].name,
			NULLQUARK
		},
		(XrmClass[]){
			application.class,
			resources[res].class,
			NULLQUARK
		},
		&tmp,
		&xval
	) ? xval.addr : NULL;
}

static void
setfont(const char *facename, double facesize)
{
	CtrlFontSet *fset;

	if (facename == NULL)
		facename = "xft:";
	fset = ctrlfnt_open(
		dpy,
		screen,
		visual,
		colormap,
		facename,
		facesize
	);
	if (fset == NULL)
		return;
	ctrlfnt_free(fontset);
	fontset = fset;
	fonth = ctrlfnt_height(fontset);
}

static void
parseresources(const char *str)
{
	XrmDatabase xdb;
	const char *value;
	char *endp;
	enum Resource res;
	XRenderColor *color;
	double d;
	int n, *num;

	if (str == NULL)
		return;
	if ((xdb = XrmGetStringDatabase(str)) == NULL)
		return;
	for (res = 0; res < NRESOURCES; res++) {
		if ((value = getresource(xdb, res)) == NULL)
			continue;
		switch (res) {
		case RES_ALIGNMENT:
			if (strcasecmp(value, "Left") == 0)
				alignment = LeftAlignment;
			else if (strcasecmp(value, "Center") == 0)
				alignment = CenterAlignment;
			else if (strcasecmp(value, "Right") == 0)
				alignment = RightAlignment;
			else
				warnx("%s: unknown alignment", value);
			break;
		case RES_BACKGROUND:
		case RES_FOREGROUND:
		case RES_BORDERCLR:
			if (res == RES_BACKGROUND)
				color = &background;
			else if (res == RES_FOREGROUND)
				color = &foreground;
			else if (res == RES_BORDERCLR)
				color = &borderclr;
			setcolor(color, value);
			break;
		case RES_OPACITY:
			d = strtod(value, &endp);
			if (d > 1.0 || d < 0.0 || endp == value)
				warnx("%s: invalid opacity", value);
			else
				opacity = (unsigned short)(0xFFFF * d);
			break;
		case RES_MAXHEIGHT:
		case RES_LEADING:
		case RES_GAP:
		case RES_PADDING:
		case RES_BORDERWID:
		case RES_IMAGEWID:
			if (res == RES_MAXHEIGHT)
				num = &max_height;
			else if (res == RES_LEADING)
				num = &leading_pixels;
			else if (res == RES_GAP)
				num = &gap_pixels;
			else if (res == RES_PADDING)
				num = &padding_pixels;
			else if (res == RES_BORDERWID)
				num = &border_pixels;
			else if (res == RES_IMAGEWID)
				num = &image_pixels;
			n = strtol(value, &endp, 10);
			if (n > 1024 || n < 0 || endp == value)
				warnx("%s: invalid width", value);
			else
				*num = n;
			break;
		case RES_GRAVITY:
			parsegravityspec(&gravity, &direction, value);
			break;
		case RES_SHRINK:
			shrink = strcasecmp(value, "true") == 0
				|| strcasecmp(value, "on") == 0
				|| strcasecmp(value, "1") == 0;
			break;
		case RES_FACENAME:
			setfont(value, 0.0);
			break;
		case NRESOURCES:
			/* ignore */
			break;
		}
	}
	XrmDestroyDatabase(xdb);
}

static void
initellipsis(void)
{
	ellipsis.s = "…";
	ellipsis.len = strlen(ellipsis.s);
	ellipsis.width = ctrlfnt_width(fontset, ellipsis.s, ellipsis.len);
}

static void
setup(void)
{
	static char *atomnames[NATOMS] = {
#define X(atom) [atom] = #atom,
		ATOMS
#undef  X
	};
	static struct {
		const char *class, *name;
		unsigned long value;
	} resdefs[NRESOURCES] = {
#define X(i, c, n, v) [i] = { .class = c, .name = n, .value = v, },
		RESOURCES
#undef  X
	};
	XRenderColor *color;
	XVisualInfo vinfo;
	Colormap cmap;
	int success;
	enum Resource res;

	ctrlfnt_init();
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(EXIT_FAILURE, "could not open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	xfd = XConnectionNumber(dpy);
	XSelectInput(dpy, root, StructureNotifyMask | PropertyChangeMask);

	/* intern atoms */
	if (!XInternAtoms(dpy, atomnames, NATOMS, False, atoms))
		errx(EXIT_FAILURE, "could not intern X atoms");

	/* intern quarks and set colors */
	XrmInitialize();
	application.class = XrmPermStringToQuark(APP_CLASS);
	application.name = XrmPermStringToQuark(APP_NAME);
	for (res = 0; res < NRESOURCES; res++) {
		resources[res].class = XrmPermStringToQuark(resdefs[res].class);
		resources[res].name = XrmPermStringToQuark(resdefs[res].name);
		switch (res) {
		case RES_ALIGNMENT:
			alignment = resdefs[res].value;
			break;
		case RES_BACKGROUND:
		case RES_FOREGROUND:
		case RES_BORDERCLR:
			if (res == RES_BACKGROUND)
				color = &background;
			else if (res == RES_FOREGROUND)
				color = &foreground;
			else if (res == RES_BORDERCLR)
				color = &borderclr;
			*color = (XRenderColor){
				.red   = RED(resdefs[res].value),
				.green = GREEN(resdefs[res].value),
				.blue  = BLUE(resdefs[res].value),
				.alpha = 0xFFFF,
			};
			break;
		case RES_LEADING:
			leading_pixels = resdefs[res].value;
			break;
		case RES_OPACITY:
			opacity = resdefs[res].value;
			break;
		case RES_PADDING:
			padding_pixels = resdefs[res].value;
			break;
		case RES_SHRINK:
			shrink = resdefs[res].value;
			break;
		case RES_IMAGEWID:
			image_pixels = resdefs[res].value;
			break;
		case RES_BORDERWID:
			border_pixels = resdefs[res].value;
			break;
		case RES_MAXHEIGHT:
			max_height = resdefs[res].value;
			break;
		case RES_GAP:
			gap_pixels = resdefs[res].value;
			break;
		case RES_GRAVITY:
			gravity = resdefs[res].value;
			break;
		case RES_FACENAME:
		case NRESOURCES:
			/* ignore */
			break;
		}
	}

	success = XMatchVisualInfo(
		dpy,
		screen,
		32,             /* preferred depth */
		TrueColor,
		&vinfo
	);
	cmap = success ? XCreateColormap(
		dpy,
		root,
		vinfo.visual,
		AllocNone
	) : None;
	if (success && cmap != None) {
		colormap = cmap;
		visual = vinfo.visual;
		depth = vinfo.depth;
	} else {
		colormap = XDefaultColormap(dpy, screen);
		visual = XDefaultVisual(dpy, screen);
		depth = XDefaultDepth(dpy, screen);
	}
	xformat = XRenderFindVisualFormat(dpy, visual);
	if (xformat == NULL)
		errx(EXIT_FAILURE, "could not find XRender visual format");
	alphaformat = XRenderFindStandardFormat(dpy, PictStandardA8);
	if (alphaformat == NULL)
		errx(EXIT_FAILURE, "could not find XRender visual format");
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(visual);
	imlib_context_set_colormap(colormap);

	parseresources(XResourceManagerString(dpy));
	if (fontset == NULL)
		setfont(NULL, 0.0);
	if (fontset == NULL) {
		errx(EXIT_FAILURE, "could not load any font");
	}
}

static void
cleanup(void)
{
	XFreeColormap(dpy, colormap);
	XCloseDisplay(dpy);
}

static void
readevent(struct Queue *queue)
{
	struct Item *item;
	struct Itemspec *itemspec;
	char *name;
	XEvent ev;

	while (XPending(dpy) && !XNextEvent(dpy, &ev)) switch (ev.type) {
	case ButtonPress:
		if ((item = getitem(queue, ev.xbutton.window)) == NULL)
			break;
		if ((ev.xbutton.button == actionbutton) && item->cmd)
			cmditem(item);
		delitem(queue, item);
		break;
	case MotionNotify:
		if ((item = getitem(queue, ev.xmotion.window)) != NULL)
			resettime(item);
		break;
	case ConfigureNotify:   /* monitor arrangement changed */
		if (ev.xconfigure.window == root) {
			initmonitor();
			queue->change = true;
		}
		break;
	case PropertyNotify:
		if (ev.xproperty.state != PropertyNewValue)
			break;
		if (ev.xproperty.window != root)
			break;
		if (rflag && ev.xproperty.atom == XA_WM_NAME) {
			if ((name = gettextprop(root, XA_WM_NAME)) == NULL)
				break;
			if ((itemspec = parseline(name)) != NULL) {
				if (oflag) {
					cleanitems(queue, NULL);
				} else if (itemspec->tag) {
					cleanitems(queue, itemspec->tag);
				}
				additem(queue, itemspec);
				free(itemspec);
			}
			queue->change = true;
			free(name);
		}
		break;
	}
}

int
main(int argc, char *argv[])
{
	struct Itemspec *itemspec;
	struct Queue *queue;    /* it contains the queue of notifications and their geometry */
	struct pollfd pfd[2];   /* [2] for stdin and xfd, see poll(2) */
	char buf[BUFSIZ];       /* buffer for stdin */
	int timeout = -1;       /* maximum interval for poll(2) to complete */
	int flags;              /* status flags for stdin */
	int reading = 1;        /* set to 0 when stdin reaches EOF */

	saveargc = argc;
	saveargv = argv;
	setup();
	parseoptions(argc, argv);

	initsignal();
	initmonitor();
	initellipsis();

	/* set up queue of notifications */
	queue = setqueue();

	/* Make stdin nonblocking */
	if ((flags = fcntl(STDIN_FILENO, F_GETFL)) == -1)
		err(1, "could not get status flags for stdin");
	if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1)
		err(1, "could not set status flags for stdin");

	/* disable buffering */
	setbuf(stdin, NULL);

	/* prepare the structure for poll(2) */
	pfd[0].fd = STDIN_FILENO;
	pfd[1].fd = xfd;
	pfd[0].events = pfd[1].events = POLLIN;

	/* run main loop */
	sigflag = SIGNAL_NONE;
	do {
		if (poll(pfd, 2, timeout) > 0) {
			if (pfd[0].revents & POLLHUP) {
				pfd[0].fd = -1;
				reading = 0;
			}
			if (pfd[0].revents & POLLIN) {
				if (fgets(buf, sizeof buf, stdin) == NULL) {
					pfd[0].fd = -1;
					reading = 0;
					continue;
				}
				if ((itemspec = parseline(buf)) != NULL) {
					if (oflag) {
						cleanitems(queue, NULL);
					} else if (itemspec->tag) {
						cleanitems(queue, itemspec->tag);
					}
					additem(queue, itemspec);
					free(itemspec);
				}
			}
			if (pfd[1].revents & POLLIN) {
				readevent(queue);
			}
		}
		if (sigflag != SIGNAL_NONE) {
			switch (sigflag) {
			case SIGNAL_CMD:
				if (queue->head != NULL)
					cmditem(queue->head);
				/* FALLTHROUGH */
			case SIGNAL_KILL:
				if (queue->head != NULL)
					delitem(queue, queue->head);
				break;
			case SIGNAL_KILLALL:
				cleanitems(queue, NULL);
				break;
			}
			sigflag = SIGNAL_NONE;
		}
		timeitems(queue);
		if (queue->change)
			moveitems(queue);
		timeout = (queue->head) ? 1000 : -1;
		XFlush(dpy);
	} while (rflag || reading || queue->head);

	/* clean up stuff */
	cleanitems(queue, NULL);
	free(queue);
	cleanup();

	return 0;
}
