#ifndef ICONS_H
#define ICONS_H

/* tabler icons 3.24.0, https://tabler.io/icons (MIT) */

#define ICON(body) \
	"<svg viewBox=\"0 0 24 24\" class=\"icon\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" " \
	"stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\">" body "</svg>"

#define ICON_SEARCH ICON("<path d=\"M10 10m-7 0a7 7 0 1 0 14 0a7 7 0 1 0 -14 0\"/><path d=\"M21 21l-6 -6\"/>")

#define ICON_IMAGES \
	ICON( \
	    "<path d=\"M15 8h.01\"/><path d=\"M3 6a3 3 0 0 1 3 -3h12a3 3 0 0 1 3 3v12a3 3 0 0 1 -3 3h-12a3 3 0 0 1 -3 " \
	    "-3v-12z\"/><path d=\"M3 16l5 -5c.928 -.893 2.072 -.893 3 0l5 5\"/><path d=\"M14 14l1 -1c.928 -.893 " \
	    "2.072 -.893 3 0l3 3\"/>")

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
	    "<path d=\"M9 3l6 0\"/><path d=\"M10 9l4 0\"/><path d=\"M10 3v6l-4 11a.7 .7 0 0 0 .5 1h11a.7 .7 0 0 0 .5 " \
	    "-1l-4 -11v-6\"/>")

#define ICON_CLOSE ICON("<path d=\"M18 6l-12 12\"/><path d=\"M6 6l12 12\"/>")

#define ICON_BACK ICON("<path d=\"M15 6l-6 6l6 6\"/>")

#define ICON_FORWARD ICON("<path d=\"M9 6l6 6l-6 6\"/>")

#define ICON_ARROW_NE ICON("<path d=\"M17 7l-10 10\"/><path d=\"M8 7l9 0l0 9\"/>")

#endif
