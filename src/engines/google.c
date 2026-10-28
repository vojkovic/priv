#include <string.h>
#include <strings.h>

#include "engines/engine.h"
#include "engines/google.h"

//
// Google's basic html page
//
// google's ad preview flags (see google.h) make it answer with the plain
// html page, whatever user agent we send. the class names in it are
// scrambled, when results stop showing up look at those first. it is an
// undocumented flag, google could take it away.
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
	struct xnodes cards;
	htmlDocPtr doc;
	const char *url;
	char *title, *content;
	int i;

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
	// a card is a result with a link, anything else in the page (related
	// searches, ads) does not have that class
	//
	cards = xpath((xmlNodePtr)doc, "//div[contains(@class, \"zMzFAb\") and .//a[contains(@class, \"fuLhoc\")]]");
	for (i = 0; i < cards.n; i++) {
		url = xpath_first_text(j->a, cards.v[i], ".//a[contains(@class, \"fuLhoc\")]/@href");
		url = unwrap_url(j->a, url ? url : "");
		title = xpath_first_text(j->a, cards.v[i], ".//span[contains(@class, \"CVA68e\")]");
		content = xpath_first_text(j->a, cards.v[i],
		    ".//span[contains(@class, \"FrIlee\")]/span[contains(@class, \"fYyStc\") and not(contains(@class, \"YVIcad\"))]");
		// no title or a link back to google, not a result
		if (!title || !*title || !publisher_url(j->a, url))
			continue;
		add_main(j, url, title, content);
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

	//
	// Build the request
	//
	form_add(&q, "q", j->query);

	// sca_esv has to be there (any value) or it is a 403
	form_add(&q, "sca_esv", "1");
	form_add(&q, "hl", l->google_hl);
	form_add(&q, "lr", l->google_lr);
	if (l->google_cr)
		form_add(&q, "cr", l->google_cr);
	form_add(&q, "ie", "utf8");
	form_add(&q, "oe", "utf8");
	// ten results a page, start is where this one begins
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", (j->pageno - 1) * 10);
		form_add(&q, "start", page);
	}
	if ((t = time_range_code(j->filters, "d", "w", "m", "y")))
		form_add(&q, "tbs", aprintf(j->a, "qdr:%s", t));
	// the flags go on the very end, "preview" last
	if (j->filters->safesearch)
		form_add(&q, "safe", "active");
	f = job_request(j, aprintf(j->a, "https://www.google.com/search?%s%s", buf_str(&q), GOOGLE_ADTEST));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_google = {
    .name = "google",
    .shortcut = "g",
    .website = "www.google.com",
    .safesearch = 1,
    .bang = "https://www.google.com/search?q=%s",
    .category = "general",
    .paging = 1,
    .max_page = 50,
    .time_range = 1,
    .search = search,
};
