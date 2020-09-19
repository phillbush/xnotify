enum {DownWards, UpWards};

/* configuration structure */
struct Config {
	const char *font;

	const char *background_color;
	const char *foreground_color;
	const char *border_color;

	const char *geometryspec;
	const char *gravityspec;

	int border_pixels;
	int gap_pixels;

	int sec;
	int onetime;
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

	/* fonts */
	FcPattern *pattern;
	XftFont **fonts;
	size_t nfonts;
};

/* geometry structure */
struct Geometry {
	int gravity;    /* NorthEastGravity, NorthGravity, etc */
	int direction;  /* DownWards or UpWards */
	int x, y;
	int w, h;
	int imagesize;
	int pad;        /* padding */
};

/* notification item structure */
struct Item {
	struct Item *prev, *next;

	char *title;
	char *body;

	time_t time;

	Imlib_Image image;
	Drawable pixmap;
	Window win;
};
