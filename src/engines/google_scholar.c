#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engines/engine.h"

//
// Google Scholar's html
//
// it is the one engine that does better with a plain request than a
// browser-like one (see search()). it needs no tricks otherwise.
//

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes items;
	struct main_result *m;
	htmlDocPtr doc;
	const char *title;
	char *byline;
	int i;

	if (!job_http_ok(j, f))
		return;

	//
	// Detect captcha
	//
	if (f->status == 302 || strstr(fetch_url(f), "/sorry/") || strstr(fetch_body(f), "gs_captcha_f")) {
		job_fail(j, "CAPTCHA", suspend_captcha);
		return;
	}
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}

	//
	// Get results
	//
	items = xpath((xmlNodePtr)doc, "//div[@data-rp]");
	for (i = 0; i < items.n; i++) {
		// no title link means a citation or a book, skip it
		title = xpath_text(j->a, items.v[i], ".//h3[1]//a");
		if (!title || !*title)
			continue;
		m = add_paper(j, xpath_first_text(j->a, items.v[i], ".//h3[1]//a/@href"), title,
		    xpath_text(j->a, items.v[i], ".//div[@class='gs_rs']"));
		if (!m)
			continue;
		/* the green line reads "authors - journal, year - publisher" */
		byline = xpath_text(j->a, items.v[i], ".//div[@class='gs_a']");
		if (byline && *byline)
			result_attr(j, m, "Author", byline);
		// a [PDF] tag next to the title means there is a direct link
		if (strstr(xpath_text(j->a, items.v[i], ".//span[@class='gs_ctg2']"), "PDF"))
			m->pdf_url = xpath_first_text(j->a, items.v[i], ".//div[@class='gs_or_ggsm']/a/@href");
	}
	xnodes_free(&items);
	job_done(j);
}

static void
search(struct job *j)
{
	time_t now = time(NULL);
	struct tm tm;
	char *ua;
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "hl", j->filters->locale->google_hl);
	form_add(&q, "start", aprintf(j->a, "%d", (j->pageno - 1) * 10));
	form_add(&q, "as_sdt", "2007");
	form_add(&q, "as_vis", "0");
	/* scholar only filters by year, so every range means since last year */
	if (j->filters->time_range && gmtime_r(&now, &tm))
		form_add(&q, "as_ylo", aprintf(j->a, "%d", tm.tm_year + 1900 - 1));
	f = job_request(j, aprintf(j->a, "https://scholar.google.com/scholar?%s", buf_str(&q)));
	buf_free(&q);
	/* scholar sends a browser-like tls fingerprint straight to its captcha, but answers a plain request */
	fetch_impersonate(f, NULL, 0);
	ua = gen_useragent();
	fetch_header(f, "User-Agent: %s", ua);
	free(ua);
	job_send(j, f, response);
}

struct engine engine_google_scholar = {
    .name = "google scholar",
    .shortcut = "gos",
    .website = "scholar.google.com",
    .bang = "https://scholar.google.com/scholar?q=%s",
    .category = "research",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
