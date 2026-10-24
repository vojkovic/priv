#include <string.h>

#include "../engine.h"

#define BASE_URL "https://suche.t-online.de"

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes docs;
	htmlDocPtr doc;
	int i;

	if (!job_http_ok(j, f))
		return;
	/* too many searches from one address get a recaptcha page instead, sent as 201 */
	if (strstr(fetch_body(f), "g-recaptcha")) {
		job_fail(j, "CAPTCHA", suspend_captcha);
		return;
	}
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	docs = xpath((xmlNodePtr)doc, "//div[@id='google_re']/div[contains(@class, 'doc')]");
	for (i = 0; i < docs.n; i++)
		add_main(j, xpath_text(j->a, docs.v[i], "./a/@href"),
		    xpath_text(j->a, docs.v[i], ".//span[contains(@class, 'tMMReshl')]"),
		    xpath_text(j->a, docs.v[i], ".//div[contains(@class, 'tMMRest')]"));
	xnodes_free(&docs);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "mandant", "toi");
	form_add(&q, "dia", "suche");
	form_add(&q, "ptl", "std");
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno));
	if (j->filters->time_range)
		form_add(&q, "age", (char[]){j->filters->time_range[0], 0});
	f = job_request(j, aprintf(j->a, BASE_URL "/web?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_tonline = {
    .name = "t online",
    .shortcut = "tol",
    .website = "suche.t-online.de",
    .bang = "https://suche.t-online.de/web?q=%s",
    .categories = {"general"},
    .paging = 1,
    .time_range = 1,
    .search = search,
};
