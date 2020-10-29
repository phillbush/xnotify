enum ItemOption {IMG, BG, FG, BRD, TAG, CMD, UNKNOWN};
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

	int alignment;
	int shrink;

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
	int texth;          /* text height, also used for padding */
};

/* notification item specification structure */
struct Itemspec {
	char *title;
	char *body;
	char *file;
	char *background;
	char *foreground;
	char *border;
	char *tag;
	char *cmd;
};

/* notification item structure */
struct Item {
	struct Item *prev, *next;

	char *title;
	char *body;
	char *tag;
	char *cmd;

	time_t time;

	int w, h;
	int imgw, imgh;

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
