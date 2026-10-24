#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libxml/parser.h>

#include "autocomplete.h"
#include "bangs.h"
#include "botdetect.h"
#include "engine.h"
#include "ev.h"
#include "favicon.h"
#include "favicon_svg.h"
#include "fetch.h"
#include "http.h"
#include "locales.h"
#include "proxy.h"
#include "render.h"
#include "script.h"
#include "search.h"
#include "style.h"

#define HTML_HEADERS "Content-Type: text/html; charset=utf-8\r\n" SECURITY_HEADERS

static char style_etag[16], script_etag[16], favicon_etag[16];

static void
respond_buf(struct conn *c, int status, const char *headers, struct buf *b)
{
	http_respond(c, status, headers, buf_str(b), b->len);
	buf_free(b);
}

static void
not_found(struct conn *c)
{
	http_respond(c, 404, "Content-Type: text/plain\r\n", "Not Found", 9);
}

static void
static_file(struct conn *c, struct request *r, const char *ctype, const char *etag, const unsigned char *data,
    size_t len)
{
	struct buf b = {0};

	if (!strcmp(r->if_none_match, etag)) {
		http_respond(c, 304, NULL, NULL, 0);
		return;
	}
	buf_printf(&b, "Content-Type: %s\r\nCache-Control: no-cache\r\nETag: %s\r\n", ctype, etag);
	http_respond(c, 200, buf_str(&b), (const char *)data, len);
	buf_free(&b);
}

static void
etag(char out[16], const unsigned char *data, size_t len)
{
	uint32_t h = 2166136261u;
	size_t i;

	for (i = 0; i < len; i++)
		h = (h ^ data[i]) * 16777619u;
	snprintf(out, 16, "\"%08x\"", h);
}

static void
preferences(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct prefs p;
	const struct locale *browser;
	const char *error = NULL;
	int secure = !strcmp(r->proto, "https");

	prefs_parse(&a, r, &p);
	if (query_param(&a, r->query, "reset")) {
		http_respond(c, 302, aprintf(&a, "Location: /preferences\r\n%s", prefs_cookie(&a, NULL, secure)), "",
		    0);
	} else if (query_param(&a, r->query, "save") && !(error = prefs_from_form(&a, r, &p))) {
		http_respond(c, 302, aprintf(&a, "Location: /preferences?saved=1\r\n%s", prefs_cookie(&a, &p, secure)),
		    "", 0);
	} else {
		if (!(browser = client_locale(&a, r->accept_language)))
			browser = &locales[0];
		render_preferences(&b, &p, browser, query_param(&a, r->query, "saved") ? "Preferences saved." : NULL,
		    error);
		respond_buf(c, 200, HTML_HEADERS, &b);
	}
	arena_free(&a);
}

static void
route(struct conn *c, struct request *r)
{
	struct arena a = {0};
	struct buf b = {0};
	struct filters f, scratch;
	struct prefs prefs;
	const char *q, *p;
	int pageno = 1;
	size_t n;

	if (botdetect(c, r))
		return;
	n = strlen(r->path);
	if (starts_with(r->path, "/client") && n > 11 && !strcmp(r->path + n - 4, ".css")) {
		botdetect_ping(c, r, astrndup(&a, r->path + 7, n - 11));
		arena_free(&a);
		return;
	}
	if (!strcmp(r->path, "/style.css")) {
		static_file(c, r, "text/css", style_etag, style_css, sizeof(style_css));
		return;
	}
	if (!strcmp(r->path, "/favicon.svg")) {
		static_file(c, r, "image/svg+xml", favicon_etag, favicon_svg, sizeof(favicon_svg));
		return;
	}
	if (!strcmp(r->path, "/privy.js")) {
		static_file(c, r, "text/javascript", script_etag, privy_js, sizeof(privy_js));
		return;
	}
	if (!strcmp(r->path, "/autocompleter")) {
		autocomplete(c, r);
		return;
	}
	if (!strcmp(r->path, "/image")) {
		proxy_image(c, r);
		return;
	}
	if (!strcmp(r->path, "/favicon")) {
		favicon_serve(c, r);
		return;
	}
	if (!strcmp(r->path, "/opensearch.xml")) {
		render_opensearch(&b, r->proto, r->host[0] ? r->host : "localhost");
		respond_buf(c, 200, "Content-Type: application/opensearchdescription+xml\r\n", &b);
		return;
	}
	if (!strcmp(r->path, "/about")) {
		prefs_parse(&a, r, &prefs);
		render_about(&b, &prefs, aprintf(&a, "%s://%s", r->proto, *r->host ? r->host : "localhost"));
		respond_buf(c, 200, HTML_HEADERS, &b);
		arena_free(&a);
		return;
	}
	if (!strcmp(r->path, "/preferences")) {
		preferences(c, r);
		return;
	}
	if (strcmp(r->path, "/") && strcmp(r->path, "/search")) {
		not_found(c);
		return;
	}
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
	filters_parse(&a, r, &f);
	scratch = f;
	if (!*query_parse(&a, q, &scratch)) {
		http_respond(c, 302, "Location: /\r\n", "", 0);
		arena_free(&a);
		return;
	}
	search_start(c, q, pageno, &f);
	arena_free(&a);
}

int
main(void)
{
	const char *config = config_load();
	size_t i;

	if (listen_port <= 0 || listen_port > 65535)
		die("config.toml: port must be between 1 and 65535");

	setlocale(LC_CTYPE, "C.UTF-8");
	signal(SIGPIPE, SIG_IGN);
	srandom(time(NULL) ^ getpid());
	etag(style_etag, style_css, sizeof(style_css));
	etag(script_etag, privy_js, sizeof(privy_js));
	etag(favicon_etag, favicon_svg, sizeof(favicon_svg));

	xmlInitParser();
	proxy_init();
	botdetect_init();
	ev_init();
	fetch_init();
	favicon_init();
	engines_init();
	for (i = 0; i < nengines; i++)
		favicon_prefetch(engines[i]->website);
	http_listen(listen_addr, listen_port, route);
	if (strchr(listen_addr, ':'))
		fprintf(stderr, "%s listening on http://[%s]:%d", instance_name, listen_addr, listen_port);
	else
		fprintf(stderr, "%s listening on http://%s:%d", instance_name, listen_addr, listen_port);
	fprintf(stderr, " (%s)\n", config ? config : "built-in settings");
	ev_run();
	return 0;
}
