#include <string.h>
#include <strings.h>

#include "engines/engine.h"
#include "engines/google.h"

//
// Google News, the same basic html as google.c
//
// the same ad preview flags as google.c (see google.h), on the news tab
// (tbm=nws). the class names are scrambled, when results stop showing up
// look at those first.
// the same story shows up more than once, so repeats are dropped.
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
	struct buf content = {0};
	struct xnodes links;
	htmlDocPtr doc;
	const char *url, *parts[3];
	char *title, *seen = NULL;
	size_t k;
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
	links = xpath((xmlNodePtr)doc, "//a[contains(@href, \"/url?\") and .//span[contains(@class, \"CVA68e\")]]");
	for (i = 0; i < links.n; i++) {
		url = xpath_text(j->a, links.v[i], "@href");
		url = unwrap_url(j->a, url);
		title = xpath_first_text(j->a, links.v[i], ".//span[contains(@class, \"CVA68e\")]");
		if (!title || !*title || !publisher_url(j->a, url))
			continue;
		// already had this one
		if (seen && strstr(seen, aprintf(j->a, "\n%s\n", url)))
			continue;
		seen = aprintf(j->a, "%s\n%s\n", seen ? seen : "", url);
		parts[0] = xpath_first_text(j->a, links.v[i], ".//span[contains(@class, \"dXDvrc\")]");
		parts[1] = xpath_first_text(j->a, links.v[i], ".//span[contains(@class, \"YVIcad\")]");
		parts[2] = xpath_first_text(j->a, links.v[i],
		    ".//span[contains(@class, \"FrIlee\")]/span[contains(@class, \"fYyStc\") and "
		    "not(contains(@class, \"YVIcad\")) and not(contains(@class, \"M3vVJe\")) and "
		    "not(contains(@class, \"dXDvrc\"))]");
		if (parts[2] && !parts[2][strspn(parts[2], " \xc2\xb7")])
			parts[2] = NULL;

		// the text under a story is the source, the age and a snippet. join what is there
		buf_free(&content);
		for (k = 0; k < LEN(parts); k++) {
			if (!parts[k] || !*parts[k])
				continue;
			if (content.len)
				buf_puts(&content, " / ");
			buf_puts(&content, parts[k]);
		}
		add_main(j, url, title, buf_str(&content));
	}
	buf_free(&content);
	xnodes_free(&links);
	job_done(j);
}

static void
search(struct job *j)
{
	const struct locale *l = j->filters->locale;
	struct buf q = {0};
	struct fetch *f;
	char page[16];

	form_add(&q, "q", j->query);
	form_add(&q, "sca_esv", "1");
	form_add(&q, "hl", l->google_hl);
	form_add(&q, "ie", "utf8");
	form_add(&q, "oe", "utf8");
	form_add(&q, "tbm", "nws");
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", (j->pageno - 1) * 10);
		form_add(&q, "start", page);
	}
	f = job_request(j, aprintf(j->a, "https://www.google.com/search?%s%s", buf_str(&q), GOOGLE_ADTEST));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_google_news = {
    .name = "google news",
    .shortcut = "gon",
    .website = "news.google.com",
    .bang = "https://news.google.com/search?q=%s",
    .category = "news",
    .paging = 1,
    .max_page = 50,
    .search = search,
};
