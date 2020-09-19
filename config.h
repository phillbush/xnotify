static struct Config config = {
	/* fonts, separate different fonts with comma */
	.font = "monospace:size=9",

	/* colors */
	.background_color = "#000000",
	.foreground_color = "#FFFFFF",
	.border_color = "#3465a4",

	/* geometry and gravity (see the manual) */
	.geometryspec = "0x0+0+0",
	.gravityspec = "NE",

	/* size of border and gaps, in pixels, */
	.border_pixels = 2,
	.gap_pixels = 7,

	/* time, in seconds, for a notification to stay alive */
	.sec = 10
};
