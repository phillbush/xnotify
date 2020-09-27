static struct Config config = {
	/* fonts, separate different fonts with comma */
	.titlefont = "monospace:size=9:style=bold",
	.bodyfont = "monospace:size=9",

	/* colors */
	.background_color = "#000000",
	.foreground_color = "#FFFFFF",
	.border_color = "#3465a4",

	/* geometry and gravity (see the manual) */
	.geometryspec = "0x0+0+0",
	.gravityspec = "NE",

	/* size of border, gaps and image (in pixels) */
	.border_pixels = 2,
	.gap_pixels = 7,
	.image_pixels = 80,     /* if 0, the image will fit the notification */
	.leading_pixels = 5,    /* space between title and body texts */
	.padding_pixels = 10,   /* space around content */

	/* text alignment, set to LeftAlignment, CenterAlignment or RightAlignment */
	.alignment = RightAlignment,

	/* set to nonzero to shrink notification width to its content size */
	.shrink = 0,

	/* time, in seconds, for a notification to stay alive */
	.sec = 10
};
