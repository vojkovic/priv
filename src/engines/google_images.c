#include <string.h>

#include "../engine.h"

static int
ok(struct job *j, struct fetch *f)
{
	struct url u;

	if (!job_http_ok(j, f))
		return 0;
	url_parse(j->a, fetch_url(f), &u);
	if (!strcmp(u.netloc, "sorry.google.com") || starts_with(u.path, "/sorry") || f->status == 302 ||
	    (f->body.len < 2000 && strstr(fetch_body(f), "/sorry/"))) {
		job_fail(j, "CAPTCHA", suspend_captcha);
		return 0;
	}
	return 1;
}

static void
response(struct job *j, struct fetch *f)
{
	struct main_result *m;
	struct xnodes tiles;
	htmlDocPtr doc;
	xmlNodePtr n;
	char *w, *h, *tbnid;
	int i;

	if (!ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	tiles = xpath((xmlNodePtr)doc, "//div[contains(@class, \"islrtb\") and @data-ou and @data-ru]");
	for (i = 0; i < tiles.n; i++) {
		n = tiles.v[i];
		m = add_image(j, xpath_text(j->a, n, "@data-ru"), xpath_text(j->a, n, "@data-pt"),
		    xpath_text(j->a, n, "@data-ou"));
		if (*(tbnid = xpath_text(j->a, n, "@data-tbnid")))
			m->thumbnail_src = aprintf(j->a, "https://encrypted-tbn0.gstatic.com/images?q=tbn:%s", tbnid);
		w = xpath_text(j->a, n, "@data-ow");
		h = xpath_text(j->a, n, "@data-oh");
		if (*w && *h)
			m->resolution = aprintf(j->a, "%s x %s", w, h);
	}
	xnodes_free(&tiles);
	job_done(j);
}

static void
search(struct job *j)
{
	const struct locale *l = j->filters->locale;
	struct buf q = {0};
	struct fetch *f;
	const char *t;
	char page[16];

	form_add(&q, "q", j->query);
	form_add(&q, "sca_esv", "1");
	form_add(&q, "hl", l->google_hl);
	form_add(&q, "ie", "utf8");
	form_add(&q, "oe", "utf8");
	form_add(&q, "tbm", "isch");
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", j->pageno - 1);
		form_add(&q, "ijn", page);
	}
	if ((t = time_range_code(j->filters, "d", "w", "m", "y")))
		form_add(&q, "tbs", aprintf(j->a, "qdr:%s", t));
	if (j->filters->safesearch)
		form_add(&q, "safe", "active");
	f = job_request(j, aprintf(j->a, "https://www.google.com/search?%s", buf_str(&q)));
	buf_free(&q);
	fetch_impersonate(f, NULL, 0);
	fetch_header(f, "User-Agent: Mozilla/4.0 (compatible; MSIE 6.0; Symbian OS) Opera 8.65");
	curl_easy_setopt(f->h, CURLOPT_FORBID_REUSE, 1L);
	f->http_version = CURL_HTTP_VERSION_3;
	job_send(j, f, response);
}

struct engine engine_google_images = {
    .name = "google images",
    .shortcut = "goi",
    .website = "images.google.com",
    .categories = {"images"},
    .paging = 1,
    .max_page = 50,
    .time_range = 1,
    .no_accept_language = 1,
    .search = search,
};
