/* macros */
#define DEFWIDTH            350     /* default width of a notification */
#define MAXLINES            128     /* maximum number of unwrapped lines */
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))

enum ItemOption {IMG, BG, FG, BRD, TAG, CMD, SEC, UNKNOWN};
enum {DownWards, UpWards};
enum {LeftAlignment, CenterAlignment, RightAlignment};
enum {
	NetWMName,
	NetWMWindowType,
	NetWMWindowTypeNotification,
	NetWMState,
	NetWMStateAbove,
	NetLast
};

/* configuration structure */
struct Config {
	const char *titlefont;
	const char *bodyfont;

	const char *background_color;
	const char *foreground_color;
	const char *border_color;

	const char *geometryspec;
	const char *gravityspec;

	int border_pixels;
	int gap_pixels;
	int image_pixels;
	int leading_pixels;
	int padding_pixels;
	int max_height;

	int alignment;
	int shrink;
	int wrap;

	int sec;

	unsigned int actionbutton;
};

/* monitor geometry structure */
struct Monitor {
	int num;
	int x, y, w, h;
};

/* draw context structure */
struct DC {
	XftColor background;
	XftColor foreground;
	XftColor border;

	GC gc;
};

/* font context structure */
struct Fonts {
	FcPattern *pattern;
	XftFont **fonts;
	size_t nfonts;
	int texth;          /* text height */
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
	int imgw, imgh;
	int textw;

	XftColor background;
	XftColor foreground;
	XftColor border;

	Imlib_Image image;
	Drawable pixmap;
	Window win;
};

/* notification queue structure */
struct Queue {
	/* queue pointers */
	struct Item *head, *tail;

	/* general geometry for the notification queue */
	int gravity;    /* NorthEastGravity, NorthGravity, etc */
	int direction;  /* DownWards or UpWards */
	int x, y;       /* position of the first notification */
	int w, h;       /* width and height of individual notifications */

	/* whether the queue changed */
	int change;
};

/* ellipsis size and font structure */
struct Ellipsis {
	char *s;
	size_t len;     /* length of s */
	int width;      /* size of ellipsis string */
	XftFont *font;  /* font containing ellipsis */
};
