#include "../engine.h"

#define BASE_URL "https://www.privacywall.org"

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes cards;
	htmlDocPtr doc;
	int i;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	cards = xpath((xmlNodePtr)doc, "//div[@id='pw-results-main']/div[contains(@class, 'result-card')]");
	for (i = 0; i < cards.n; i++)
		add_main(j, xpath_text(j->a, cards.v[i], ".//a[contains(@class, 'result-url-anchor')]/@href"),
		    xpath_text(j->a, cards.v[i],
		        ".//div[contains(concat(' ', normalize-space(@class), ' '), ' result_title ')]"),
		    xpath_text(j->a, cards.v[i],
		        ".//div[contains(concat(' ', normalize-space(@class), ' '), ' result-description ')]"));
	xnodes_free(&cards);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "safesearch", j->filters->safesearch ? "on" : "off");
	if (*j->filters->locale->privacywall_cc)
		form_add(&q, "cc", j->filters->locale->privacywall_cc);
	if (j->filters->time_range)
		form_add(&q, "time", j->filters->time_range);
	f = job_request(j, aprintf(j->a, BASE_URL "/search/secure/?%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "Referer: " BASE_URL "/");
	job_send(j, f, response);
}

/* whether this engine narrows results to the locale's language, for the preferences page */
static int
language(const struct locale *l)
{
	return *l->privacywall_cc && strcmp(l->privacywall_cc, "US") != 0;
}

struct engine engine_privacywall = {
    .name = "privacywall",
    .shortcut = "pw",
    .website = "www.privacywall.org",
    .language = language,
    .safesearch = 1,
    .bang = "https://www.privacywall.org/search/secure?q=%s",
    .categories = {"general"},
    .time_range = 1,
    .search = search,
};
