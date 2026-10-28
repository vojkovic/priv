#include <stdlib.h>
#include <string.h>

#include "engines/engine.h"

/* the "More at <source>" link that ends the abstract */
#define ABSTRACT_SOURCE "//div[@id=\"zero_click_abstract\"]/a[normalize-space()][last()]"

#define DDG_URL "https://html.duckduckgo.com/html/"

//
// DuckDuckGo's no-javascript page
//
// page one is a plain form post. later pages need the "vqd" value from the
// previous page's form, which travels in our own paging links (j->token) so
// nothing is kept on the server between searches.
//

static char *useragent;

/* a !word in the query would send duckduckgo's own bang redirect, so put quotes around them */
static const char *
quote_bangs(struct job *j, const char *query)
{
	struct buf b = {0};
	const char *p = query, *end;
	char *r;

	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (!*p)
			break;
		end = p + strcspn(p, " \t\n");
		if (b.len)
			buf_putc(&b, ' ');
		if (*p == '!' && end - p > 1)
			buf_printf(&b, "'%.*s'", (int)(end - p), p);
		else
			buf_add(&b, p, end - p);
		p = end;
	}
	r = astrdup(j->a, buf_str(&b));
	buf_free(&b);
	return r;
}

static void
response(struct job *j, struct fetch *f)
{
	htmlDocPtr doc;
	xmlNodePtr form, snippet;
	struct xnodes links;
	const char *vqd, *zurl;
	char *zero;
	int i;

	if (!job_http_ok(j, f))
		return;
	if (f->status == 303) {
		// it wants to redirect us somewhere (a bang slipped through), no results
		job_done(j);
		return;
	}
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}

	//
	// Detect captcha
	//
	if (xpath_exists((xmlNodePtr)doc, "//form[@id='challenge-form']")) {
		job_fail(j, "CAPTCHA", 0);
		return;
	}

	//
	// Get the next page's token
	//
	// no new vqd on the page means keep the old one
	//
	j->next_token = j->token;
	if ((form = xpath_first((xmlNodePtr)doc, "//input[@name=\"vqd\"]/..")) &&
	    (vqd = xpath_first_text(j->a, form, "//input[@name=\"vqd\"]/@value")))
		j->next_token = vqd;

	//
	// Get results
	//
	links = xpath((xmlNodePtr)doc, "//div[@id=\"links\"]/div[contains(@class, \"web-result\")]");
	for (i = 0; i < links.n; i++) {
		const char *url = xpath_first_text(j->a, links.v[i], ".//h2/a/@href");
		if (!url)
			continue; // not a result
		snippet = xpath_first(links.v[i], ".//a[contains(@class, \"result__snippet\")]");
		add_main(j, url, xpath_text(j->a, links.v[i], ".//h2/a"), snippet ? node_text(j->a, snippet) : "");
	}
	xnodes_free(&links);

	//
	// Get the abstract (the box at the top)
	//
	zurl = xpath_first_text(j->a, (xmlNodePtr)doc, ABSTRACT_SOURCE "/@href");
	zero = xpath_text(j->a, (xmlNodePtr)doc, ABSTRACT_SOURCE "/preceding-sibling::node()");
	// "what is my ip" style answers are ddg's, not an abstract, skip them
	if (zurl && *zero && !strstr(zero, "Your IP address is") && !strstr(zero, "Your user agent:") &&
	    !strstr(zero, "URL Decoded:"))
		add_answer(j, zero, zurl);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf data = {0};
	struct fetch *f;
	const char *q, *t, *kl = j->filters->locale->ddg_region;
	char num[16];
	int offset;

	if (strlen(j->query) >= 500) {
		// too long for the form, it would just error
		job_done(j);
		return;
	}
	if (!useragent)
		useragent = gen_useragent();
	q = quote_bangs(j, j->query);
	form_add(&data, "q", q);
	if (j->pageno == 1) {
		form_add(&data, "b", "");
	} else {
		// no token (or a chinese locale, which has no paging) means no page two
		if (!j->token || starts_with(j->filters->locale->tag, "zh")) {
			buf_free(&data);
			job_done(j);
			return;
		}
		// the form the "next" button submits
		form_add(&data, "vqd", j->token);
		form_add(&data, "nextParams", "");
		form_add(&data, "api", "d.js");
		form_add(&data, "o", "json");
		form_add(&data, "v", "l");
		// page one has 10 results, every page after that 15
		offset = 10 + (j->pageno - 2) * 15;
		snprintf(num, sizeof(num), "%d", offset + 1);
		form_add(&data, "dc", num);
		snprintf(num, sizeof(num), "%d", offset);
		form_add(&data, "s", num);
	}
	form_add(&data, "kl", kl);
	t = time_range_code(j->filters, "d", "w", "m", "y");
	if (t)
		form_add(&data, "df", t);

	// wt-wt is "no region", the default, so only send the cookie for the rest
	f = job_request(j, DDG_URL);
	if (strcmp(kl, "wt-wt"))
		fetch_cookie(f, "kl", kl);
	if (t)
		fetch_cookie(f, "df", t);
	fetch_header(f, "User-Agent: %s", useragent);
	// look like a person submitting the form on the site
	fetch_header(f, "Sec-Fetch-Dest: document");
	fetch_header(f, "Sec-Fetch-Mode: navigate");
	fetch_header(f, "Sec-Fetch-Site: same-origin");
	fetch_header(f, "Sec-Fetch-User: ?1");
	fetch_header(f, "Referer: " DDG_URL);
	fetch_post(f, buf_str(&data), "application/x-www-form-urlencoded");
	buf_free(&data);
	job_send(j, f, response);
}

struct engine engine_duckduckgo = {
    .name = "duckduckgo",
    .shortcut = "ddg",
    .website = "duckduckgo.com",
    .bang = "https://duckduckgo.com/?q=%s",
    .category = "general",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
