#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botdetection/botdetect.h"
#include "calc.h"
#include "color.h"
#include "engines/engine.h"
#include "favicon_svg.h"
#include "features/autocomplete.h"
#include "features/favicon.h"
#include "features/metrics.h"
#include "features/proxy.h"
#include "render/render.h"
#include "routes.h"
#include "script.h"
#include "search/bangs.h"
#include "search/locales.h"
#include "search/search.h"
#include "style.h"

#define HTML_HEADERS "Content-Type: text/html; charset=utf-8\r\n" SECURITY_HEADERS

static void
respond_buf(struct conn *c, int status, const char *headers, struct buf *b)
{
	http_respond(c, status, headers, buf_str(b), b->len);
	buf_free(b);
}

/* the files built into the binary */
struct asset {
	const char *path, *type;
	const unsigned char *data;
	size_t len;
	char etag[16];
};

static struct asset assets[] = {
    {"/style.css", "text/css", style_css, sizeof(style_css), ""},
    {"/favicon.svg", "image/svg+xml", favicon_svg, sizeof(favicon_svg), ""},
    {"/priv.js", "text/javascript", priv_js, sizeof(priv_js), ""},
    {"/calc.js", "text/javascript", calc_js, sizeof(calc_js), ""},
    {"/color.js", "text/javascript", color_js, sizeof(color_js), ""},
};

static void
serve_asset(struct conn *c, struct request *r, const struct asset *a)
{
	struct buf b = {0};

	if (!strcmp(r->if_none_match, a->etag)) {
		http_respond(c, 304, NULL, NULL, 0);
		return;
	}
	buf_printf(&b, "Content-Type: %s\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nETag: %s\r\n",
	    a->type, a->etag);
	http_respond(c, 200, buf_str(&b), (const char *)a->data, a->len);
	buf_free(&b);
}

static void
not_found(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct prefs p;

	prefs_parse(&a, r, &p);
	render_not_found(&b, &p);
	respond_buf(c, 404, HTML_HEADERS, &b);
	arena_free(&a);
}

static void
healthz(struct conn *c, struct request *r)
{
	(void)r;
	http_respond(c, 200, "Content-Type: text/plain\r\nCache-Control: no-store\r\n", "ok\n", 3);
}

static void
metrics(struct conn *c, struct request *r)
{
	struct buf b = {0};

	(void)r;
	metrics_render(&b);
	respond_buf(c, 200, "Content-Type: text/plain; version=0.0.4\r\nCache-Control: no-store\r\n", &b);
}

static void
opensearch(struct conn *c, struct request *r)
{
	struct buf b = {0};

	render_opensearch(&b, r->proto, r->host[0] ? r->host : "localhost");
	respond_buf(c, 200, "Content-Type: application/opensearchdescription+xml\r\n", &b);
}

static void
stats(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct prefs p;

	prefs_parse(&a, r, &p);
	render_stats(&b, &p);
	respond_buf(c, 200, HTML_HEADERS "Cache-Control: no-store\r\n", &b);
	arena_free(&a);
}

static void
about(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct prefs p;

	prefs_parse(&a, r, &p);
	render_about(&b, &p, aprintf(&a, "%s://%s", r->proto, *r->host ? r->host : "localhost"));
	respond_buf(c, 200, HTML_HEADERS, &b);
	arena_free(&a);
}

static void
preferences(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct prefs p;
	const struct locale *browser;
	int secure = !strcmp(r->proto, "https");

	prefs_parse(&a, r, &p);
	if (query_param(&a, r->query, "reset")) {
		http_respond(c, 302, aprintf(&a, "Location: /preferences\r\n%s", prefs_cookie(&a, NULL, secure)), "",
		    0);
	} else if (query_param(&a, r->query, "save")) {
		prefs_from_form(&a, r, &p);
		http_respond(c, 302, aprintf(&a, "Location: /\r\n%s", prefs_cookie(&a, &p, secure)), "", 0);
	} else {
		if (!(browser = client_locale(&a, r->accept_language)))
			browser = &locales[0];
		render_preferences(&b, &p, browser);
		respond_buf(c, 200, HTML_HEADERS, &b);
	}
	arena_free(&a);
}

/* the home page, and a search when there is a query */
static void
search(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct filters f, scratch;
	struct prefs prefs;
	const char *q, *p, *tokens;
	int pageno = 1;

	q = query_param(&a, r->query, "q");
	if (!q || !*(q = str_strip((char *)q))) {
		if (strcmp(r->path, "/")) {
			http_respond(c, 302, "Location: /\r\n", "", 0);
		} else {
			prefs_parse(&a, r, &prefs);
			render_home(&b, &prefs);
			respond_buf(c, 200, HTML_HEADERS, &b);
		}
		arena_free(&a);
		return;
	}
	if ((p = bang_redirect(&a, q))) {
		http_respond(c, 302, aprintf(&a, "Location: %s\r\n", p), "", 0);
		arena_free(&a);
		return;
	}
	if ((p = query_param(&a, r->query, "p")))
		pageno = (int)parse_long(p, 1);
	pageno = pageno < 1 ? 1 : pageno > 50 ? 50 : pageno;
	p = query_param(&a, r->query, "pt");
	tokens = p && proxy_hmac_ok('p', p, query_param(&a, r->query, "ph")) ? p : NULL;
	filters_parse(&a, r, &f);
	f.client_ip = botdetect_client_ip(r);
	f.client_agent = r->user_agent;
	f.client_language = r->accept_language;
	scratch = f;
	if (!*query_parse(&a, q, &scratch)) {
		http_respond(c, 302, "Location: /\r\n", "", 0);
		arena_free(&a);
		return;
	}
	search_start(c, q, pageno, tokens, &f);
	arena_free(&a);
}

struct route {
	const char *path;
	void (*handler)(struct conn *c, struct request *r);
	/* answered before the bot detector, which turns away the curl-like clients that check health */
	int exempt;
};

static const struct route routes[] = {
    {"/healthz", healthz, 1},
    {"/metrics", metrics, 1},
    {"/", search, 0},
    {"/search", search, 0},
    {"/autocompleter", autocomplete, 0},
    {"/image", proxy_image, 0},
    {"/favicon", favicon_serve, 0},
    {"/opensearch.xml", opensearch, 0},
    {"/stats", stats, 0},
    {"/about", about, 0},
    {"/preferences", preferences, 0},
};

void
routes_init(void)
{
	size_t i, k;
	uint32_t h;

	for (i = 0; i < LEN(assets); i++) {
		h = 2166136261u;
		for (k = 0; k < assets[i].len; k++)
			h = (h ^ assets[i].data[k]) * 16777619u;
		snprintf(assets[i].etag, sizeof(assets[i].etag), "\"%08x\"", h);
	}
}

void
route(struct conn *c, struct request *r)
{
	struct arena a = {0};
	size_t i, n;

	for (i = 0; i < LEN(routes); i++)
		if (routes[i].exempt && !strcmp(r->path, routes[i].path)) {
			routes[i].handler(c, r);
			return;
		}
	if (botdetect(c, r))
		return;
	n = strlen(r->path);
	if (starts_with(r->path, "/client") && n > 11 && !strcmp(r->path + n - 4, ".css")) {
		botdetect_ping(c, r, astrndup(&a, r->path + 7, n - 11));
		arena_free(&a);
		return;
	}
	for (i = 0; i < LEN(assets); i++)
		if (!strcmp(r->path, assets[i].path)) {
			serve_asset(c, r, &assets[i]);
			return;
		}
	for (i = 0; i < LEN(routes); i++)
		if (!routes[i].exempt && !strcmp(r->path, routes[i].path)) {
			routes[i].handler(c, r);
			return;
		}
	not_found(c, r);
}
