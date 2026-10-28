#ifndef ICONS_H
#define ICONS_H

/* tabler icons 3.24.0, https://tabler.io/icons (MIT), and feather 4.29.2, https://feathericons.com (MIT) for images */

#define ICON(body) \
	"<svg viewBox=\"0 0 24 24\" class=\"icon\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" " \
	"stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\">" body "</svg>"

#define ICON_SEARCH ICON("<path d=\"M10 10m-7 0a7 7 0 1 0 14 0a7 7 0 1 0 -14 0\"/><path d=\"M21 21l-6 -6\"/>")

#define ICON_IMAGES \
	ICON("<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\" ry=\"2\"/><circle cx=\"8.5\" cy=\"8.5\" " \
	     "r=\"1.5\"/><path d=\"M21 15l-5-5L5 21\"/>")

#define ICON_VIDEOS ICON("<path d=\"M7 4v16l13 -8z\"/>")

#define ICON_NEWS \
	ICON( \
	    "<path d=\"M16 6h3a1 1 0 0 1 1 1v11a2 2 0 0 1 -4 0v-13a1 1 0 0 0 -1 -1h-10a1 1 0 0 0 -1 1v12a3 3 0 0 0 3 " \
	    "3h11\"/><path d=\"M8 8l4 0\"/><path d=\"M8 12l4 0\"/><path d=\"M8 16l4 0\"/>")

#define ICON_MUSIC \
	ICON("<path d=\"M3 17a3 3 0 1 0 6 0a3 3 0 0 0 -6 0\"/><path d=\"M13 17a3 3 0 1 0 6 0a3 3 0 0 0 -6 0\"/>" \
	     "<path d=\"M9 17v-13h10v13\"/><path d=\"M9 8h10\"/>")

#define ICON_TECH ICON("<path d=\"M7 8l-4 4l4 4\"/><path d=\"M17 8l4 4l-4 4\"/><path d=\"M14 4l-4 16\"/>")

#define ICON_RESEARCH \
	ICON( \
	    "<path d=\"M6 21l6 -5l6 5\"/><path d=\"M12 13v8\"/><path d=\"M3.294 13.678l.166 .281c.52 .88 1.624 1.265 " \
	    "2.605 .91l14.242 -5.165a1.023 1.023 0 0 0 .565 -1.456l-2.62 -4.705a1.087 1.087 0 0 0 -1.447 -.42l-.056 " \
	    ".032l-12.694 7.618c-1.02 .613 -1.357 1.897 -.76 2.905z\"/><path d=\"M14 5l3 5.5\"/>")

#define ICON_SETTINGS \
	ICON( \
	    "<path d=\"M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 " \
	    "2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 " \
	    "1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 " \
	    "0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 " \
	    "-2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 " \
	    "-1.065z\"/><path d=\"M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0\"/>")

#define ICON_CHECK ICON("<path d=\"M5 12l5 5l10 -10\"/>")
#define ICON_MINUS ICON("<path d=\"M5 12l14 0\"/>")

#define ICON_INFO \
	ICON("<path d=\"M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0\"/><path d=\"M12 9h.01\"/><path d=\"M11 12h1v4h1\"/>")

#define ICON_CLOSE ICON("<path d=\"M18 6l-12 12\"/><path d=\"M6 6l12 12\"/>")

#define ICON_BACK ICON("<path d=\"M15 6l-6 6l6 6\"/>")

#define ICON_FORWARD ICON("<path d=\"M9 6l6 6l-6 6\"/>")

#define ICON_EXPAND ICON("<path d=\"M6 9l6 6l6 -6\"/>")

#define ICON_ARROW_NE ICON("<path d=\"M17 7l-10 10\"/><path d=\"M8 7l9 0l0 9\"/>")

#endif
