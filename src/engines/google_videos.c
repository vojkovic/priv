#include <string.h>
#include <strings.h>

#include "engines/engine.h"
#include "engines/google.h"

//
// Google Videos, the same basic html as google.c
//
// the same ad preview flags as google.c (see google.h), on the video tab
// (tbm=vid). the class names are scrambled, when results stop showing up
// look at those first.
//

/* result links are /url?q=<real address>&..., get the real one out */
static const char *
unwrap_url(struct arena *a, const char *raw)
{
	const char *target = url_query_get(a, raw, "q");

	return target && starts_with(target, "http") ? target : raw;
}

/* real pages only, not google's own images or the "unusual traffic" page */
static int
publisher_url(struct arena *a, const char *url)
{
	struct url u;
	size_t n;

	url_parse(a, url, &u);
	n = strlen(u.netloc);
	if (!strcasecmp(u.netloc, "gstatic.com") || (n > 12 && !strcasecmp(u.netloc + n - 12, ".gstatic.com")))
		return 0;
	return starts_with(url, "http") && !strstr(url, "/sorry/");
}

/* 3:05 or 1:02:03, the length is one of the grey spans under a video */
static int
is_duration(const char *s)
{
	int colons = 0;

	if (!*s)
		return 0;
	for (; *s; s++) {
		if (*s == ':')
			colons++;
		else if (!IS_DIGIT(*s))
			return 0;
	}
	return colons >= 1 && colons <= 2;
}

/* the request worked and we were not sent to the captcha */
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
	struct xnodes cards, spans;
	struct main_result *m;
	htmlDocPtr doc;
	const char *url, *length;
	char *title;
	int i, k;

	//
	// Detect captcha
	//
	if (!ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}

	//
	// Get results
	//
	cards = xpath((xmlNodePtr)doc, "//div[contains(@class, \"zMzFAb\") and .//a[contains(@class, \"fuLhoc\")]]");
	for (i = 0; i < cards.n; i++) {
		url = xpath_first_text(j->a, cards.v[i], ".//a[contains(@class, \"fuLhoc\")]/@href");
		url = unwrap_url(j->a, url ? url : "");
		title = xpath_first_text(j->a, cards.v[i], ".//span[contains(@class, \"CVA68e\")]");
		if (!title || !*title || !publisher_url(j->a, url))
			continue;
		m = add_video(j, url, title, "");

		// no field for the length, so look for the span that looks like one
		spans = xpath(cards.v[i], ".//span[contains(@class, \"YVIcad\")]");
		for (k = 0; k < spans.n; k++) {
			length = str_strip(node_text(j->a, spans.v[k]));
			if (is_duration(length)) {
				m->length = length;
				break;
			}
		}
		xnodes_free(&spans);
	}
	xnodes_free(&cards);
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
	form_add(&q, "tbm", "vid");
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", (j->pageno - 1) * 10);
		form_add(&q, "start", page);
	}
	if ((t = time_range_code(j->filters, "d", "w", "m", "y")))
		form_add(&q, "tbs", aprintf(j->a, "qdr:%s", t));
	if (j->filters->safesearch)
		form_add(&q, "safe", "active");
	f = job_request(j, aprintf(j->a, "https://www.google.com/search?%s%s", buf_str(&q), GOOGLE_ADTEST));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_google_videos = {
    .name = "google videos",
    .shortcut = "gov",
    .website = "www.google.com",
    .safesearch = 1,
    .bang = "https://www.google.com/search?tbm=vid&q=%s",
    .category = "videos",
    .paging = 1,
    .max_page = 50,
    .time_range = 1,
    .search = search,
};
