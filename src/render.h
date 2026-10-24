#ifndef RENDER_H
#define RENDER_H

#include "search.h"
#include "util.h"

#define SECURITY_HEADERS \
	"Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self' data:; script-src 'self'; connect-src 'self'; " \
	"form-action 'self' https:; base-uri 'none'; frame-ancestors 'none'\r\n" \
	"X-Content-Type-Options: nosniff\r\n" \
	"Referrer-Policy: no-referrer\r\n"

void render_home(struct buf *b, const struct prefs *p);
void render_about(struct buf *b, const struct prefs *p, const char *origin);
void render_preferences(struct buf *b, const struct prefs *p, const struct locale *browser, const char *notice,
    const char *error);
void render_head(struct buf *b, struct search *s);
void render_results(struct buf *b, struct search *s);
void render_opensearch(struct buf *b, const char *proto, const char *host);

#endif
