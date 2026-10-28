#ifndef RENDER_INTERNAL_H
#define RENDER_INTERNAL_H

#include "core/util.h"
#include "render/icons.h"
#include "search/search.h"

#define LOGO \
	"<svg class=\"logo\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"3.2 2 21.6 21.6\" aria-hidden=\"true\"><defs>" \
	"<linearGradient id=\"logo-rainbow\" gradientUnits=\"userSpaceOnUse\" x1=\"3.2\" y1=\"2\" x2=\"29.2\" y2=\"11.5\">" \
	"<stop offset=\"0\"/><stop offset=\".22\"/><stop offset=\".22\"/><stop offset=\".34\"/><stop offset=\".34\"/><stop offset=\".46\"/><stop offset=\".46\"/><stop offset=\".58\"/><stop offset=\".58\"/><stop offset=\".7\"/><stop offset=\".7\"/><stop offset=\".82\"/><stop offset=\".82\"/><stop offset=\"1\"/></linearGradient></defs>" \
	"<path fill=\"url(#logo-rainbow)\" fill-rule=\"evenodd\" stroke=\"url(#logo-rainbow)\" stroke-width=\"1.4\" " \
	"stroke-linejoin=\"round\" d=\"M6.02 7.81L5.7 2.9l4.53 2.02A7.3 7.3 0 0 1 13.77 4.92L18.3 2.9l-.32 4.91A7.3 7.3 0 1 1 " \
	"6.02 7.81zM17.5 12a5.5 5.5 0 1 0-11 0 5.5 5.5 0 0 0 11 0z\"/>" \
	"<path d=\"M17.2 17.2L21.8 21.8\" stroke=\"url(#logo-rainbow)\" stroke-width=\"3.6\" stroke-linecap=\"round\" " \
	"fill=\"none\"/></svg>"

#define SOURCE_URL "https://github.com/vojkovic/priv"

#define YES "<span class=\"yes\">" ICON_CHECK "</span>"
#define NO "<span class=\"no\">" ICON_MINUS "</span>"

/* images the proxy already prefetched go inline, the rest are fetched through it */
#define INLINE_MAX (12 << 10)
#define INFOBOX_INLINE_MAX (64 << 10)
#define FAVICON_INLINE_MAX (3 << 10)
#define FAVICON_INLINE_SITES 16

/* shared by the page files in render/ */
extern const char *safe_levels[];
extern struct arena scratch;

int self_query(const char *q);
void selfinfo(struct buf *b, struct filters *f);
void answers(struct buf *b, struct search *s);
void infobox(struct buf *b, struct search *s, struct infobox *ib);
struct infobox *shown_infobox(const struct results *r);
void doc_start(struct buf *b, const char *title, const char *body_class, const struct prefs *p);
void footer(struct buf *b);
const char *html_str(struct arena *a, const char *s);
void nav_links(struct buf *b);
void option(struct buf *b, const char *value, const char *label, int selected);
void page_start(struct buf *b, const struct prefs *p, const char *title, const char *heading);
void put_tr(struct buf *b, const char *msgid, const char *arg0, const char *arg1);
void attributes(struct buf *b, struct main_result *m);
void content(struct buf *b, struct search *s, struct main_result *m);
void favicon(struct buf *b, struct search *s, const char *authority);
const char *image_src(struct search *s, const char *url, const char *proxied, size_t max);
void category_tabs(struct buf *b, const char *current);
void filter_params(struct buf *b, const struct filters *f);
void language_options(struct buf *b, const char *selected, const char *detected);
void search_box(struct buf *b, const char *query, const char *category, int autofocus);
void search_filters(struct buf *b, const struct filters *f);

#endif
