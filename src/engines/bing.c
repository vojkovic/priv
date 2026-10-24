#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../engine.h"

#define BASE_URL "https://www.bing.com"
#define BLOCKED_SUSPEND 300

static const char *stopwords[] = {"and", "are", "for", "from", "how", "the", "was", "what", "when", "where", "which",
    "who", "why", "with", "you", "your"};

static char *
hex(struct arena *a, size_t n)
{
	char *s = amalloc(a, n + 1);
	size_t i;

	for (i = 0; i < n; i++)
		s[i] = "0123456789ABCDEF"[rnd(16)];
	s[n] = 0;
	return s;
}

/* ck/a links carry the target in u, as "a1" plus unpadded base64url */
static char *
unwrap(struct arena *a, const char *href)
{
	const char *u, *p;
	unsigned char *out;
	uint32_t acc = 0;
	size_t n = 0;
	int bits = 0, v;

	if (!starts_with(href, BASE_URL "/ck/a") || !(u = url_query_get(a, href, "u")) || !starts_with(u, "a1"))
		return (char *)href;
	out = amalloc(a, strlen(u));
	for (p = u + 2; *p; p++) {
		if (*p >= 'A' && *p <= 'Z')
			v = *p - 'A';
		else if (*p >= 'a' && *p <= 'z')
			v = *p - 'a' + 26;
		else if (IS_DIGIT(*p))
			v = *p - '0' + 52;
		else if (*p == '-' || *p == '+')
			v = 62;
		else if (*p == '_' || *p == '/')
			v = 63;
		else
			continue;
		acc = acc << 6 | (uint32_t)v;
		if ((bits += 6) >= 8)
			out[n++] = (unsigned char)(acc >> (bits -= 8));
	}
	out[n] = 0;
	return (char *)out;
}

static char *
lowered(struct arena *a, const char *s)
{
	char *r = astrdup(a, s), *p;

	for (p = r; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return r;
}

static int
stopword(const char *w)
{
	size_t i;

	for (i = 0; i < LEN(stopwords); i++)
		if (!strcmp(stopwords[i], w))
			return 1;
	return 0;
}

static int
ascii(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s >= 0x80)
			return 0;
	return 1;
}

/* words worth checking a result against: no operators, stopwords or short ascii words */
static size_t
terms(struct arena *a, const char *query, char **out, size_t max)
{
	char *q = lowered(a, query), *w, *save;
	size_t n = 0;

	for (w = strtok_r(q, " \t", &save); w && n < max; w = strtok_r(NULL, " \t", &save)) {
		if (*w == '-' || strchr(w, ':') || stopword(w) || (ascii(w) && strlen(w) < 3))
			continue;
		out[n++] = w;
	}
	return n;
}

struct hit {
	char *url, *title, *content, *text;
};

/* bing sometimes answers with someone else's query: too few results mention any of ours */
static int
unrelated(struct hit *hits, size_t nhits, char **t, size_t nt)
{
	size_t i, k, matches = 0;

	if (nhits < 3 || !nt)
		return 0;
	for (i = 0; i < nhits; i++)
		for (k = 0; k < nt; k++)
			if (strstr(hits[i].text, t[k])) {
				matches++;
				break;
			}
	return matches * 5 <= nhits;
}

/* where the results bing sent start, from its "41-54 of 148,000 results" counter */
static long
first_shown(struct arena *a, xmlNodePtr doc)
{
	const char *s = xpath_first_text(a, doc, "//span[contains(@class, 'sb_count')]"), *dash;

	if (!s || !(dash = strchr(s, '-')))
		return 1;
	while (dash > s && IS_DIGIT(dash[-1]))
		dash--;
	return strtol(dash, NULL, 10);
}

static void request(struct job *j, job_cb cb);
static void last(struct job *j, struct fetch *f);

/* bing often answers with an empty "there are no results" page that a second try gets past */
static void
response(struct job *j, struct fetch *f, int retry)
{
	struct xnodes items;
	struct hit *hits;
	struct url u;
	htmlDocPtr doc;
	const char *body = fetch_body(f), *href, *none;
	char *url, *title, *content, *t[32];
	size_t nhits = 0, nt;
	int i;

	if (!job_http_ok(j, f))
		return;
	url_parse(j->a, fetch_url(f), &u);
	if (strstr(body, "/turing/captcha") || strstr(body, "id=\"b_captcha\"") || strstr(u.path, "/challenge")) {
		job_fail(j, "CAPTCHA", suspend_captcha);
		return;
	}
	if (!(doc = job_html(j, f)) || !xpath_exists((xmlNodePtr)doc, "//ol[@id='b_results']")) {
		job_fail(j, "blocked", BLOCKED_SUSPEND);
		return;
	}
	items = xpath((xmlNodePtr)doc,
	    "//ol[@id='b_results']/li[contains(concat(' ', normalize-space(@class), ' '), ' b_algo ')]");
	if (!items.n) {
		xnodes_free(&items);
		none = xpath_first_text(j->a, (xmlNodePtr)doc, "//li[contains(@class, 'b_no')]");
		if (none && strstr(none, "Ref A:") && strstr(none, "Ref B:") && strstr(none, "Ref C:"))
			job_fail(j, "blocked", BLOCKED_SUSPEND);
		else if (retry)
			request(j, last);
		else
			job_done(j);
		return;
	}
	/* bing numbers its own pages irregularly, so check it started where we asked rather than its page label */
	if (j->pageno > 1 && first_shown(j->a, (xmlNodePtr)doc) != (j->pageno - 1) * 10 + 1) {
		xnodes_free(&items);
		job_fail(j, "wrong page", SUSPEND_NONE);
		return;
	}
	hits = amalloc(j->a, (size_t)items.n * sizeof(*hits));
	for (i = 0; i < items.n; i++) {
		href = xpath_first_text(j->a, items.v[i], "(.//h2/a/@href | .//a[h2]/@href)[1]");
		title = xpath_first_text(j->a, items.v[i], "(.//h2/a | .//a/h2)[1]");
		content = xpath_first_text(j->a, items.v[i],
		    "(.//div[contains(@class, 'b_caption')]//p | .//p[contains(@class, 'b_lineclamp')] | .//p)[1]");
		url = unwrap(j->a, href ? href : "");
		if (!starts_with(url, "http://") && !starts_with(url, "https://"))
			continue;
		if (!title || !*title)
			continue;
		hits[nhits].url = url;
		hits[nhits].title = title;
		hits[nhits].content = content;
		hits[nhits++].text = lowered(j->a, aprintf(j->a, "%s %s %s", title, content ? content : "", url));
	}
	xnodes_free(&items);
	/* the first word is checked separately too: "best laptop 2026" can come back as results for "best" */
	nt = terms(j->a, j->query, t, LEN(t));
	if (unrelated(hits, nhits, t, nt) || (nt >= 3 && unrelated(hits, nhits, t + 1, nt - 1))) {
		job_fail(j, "unrelated results", SUSPEND_NONE);
		return;
	}
	for (i = 0; i < (int)nhits; i++)
		add_main(j, hits[i].url, hits[i].title, hits[i].content);
	job_done(j);
}

static void
last(struct job *j, struct fetch *f)
{
	response(j, f, 0);
}

static void
first(struct job *j, struct fetch *f)
{
	response(j, f, 1);
}

static void
request(struct job *j, job_cb cb)
{
	static const char *safe[] = {"OFF", "DEMOTE", "STRICT"};
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	if (j->pageno == 2) {
		form_add(&q, "first", "11");
		form_add(&q, "FORM", "PORE");
	} else if (j->pageno > 2) {
		form_add(&q, "first", aprintf(j->a, "%d", (j->pageno - 1) * 10 + 1));
		form_add(&q, "FORM", aprintf(j->a, "PERE%d", j->pageno - 2));
	}
	f = job_request(j, aprintf(j->a, BASE_URL "/search?%s", buf_str(&q)));
	buf_free(&q);
	/* the cookies of a returning visitor, new each time, make bing answer properly and page past 2 */
	fetch_cookie(f, "MUID", hex(j->a, 32));
	fetch_cookie(f, "SRCHD", "AF=NOFORM");
	fetch_cookie(f, "SRCHUID", aprintf(j->a, "V=2&GUID=%s", hex(j->a, 32)));
	fetch_cookie(f, "_EDGE_S", "mkt=en-us&ui=en");
	fetch_cookie(f, "_EDGE_CD", "m=en-us&u=en");
	fetch_cookie(f, "SRCHHPGUSR", aprintf(j->a, "ADLT=%s", safe[j->filters->safesearch]));
	/* over http/2 bing ignores first= past page 2 and answers with page 1 */
	f->http_version = CURL_HTTP_VERSION_1_1;
	/* a reused connection hangs after bing has let it idle, so each search gets a fresh one */
	curl_easy_setopt(f->h, CURLOPT_FORBID_REUSE, 1L);
	curl_easy_setopt(f->h, CURLOPT_SSL_SESSIONID_CACHE, 0L);
	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	job_send(j, f, cb);
}

static void
search(struct job *j)
{
	request(j, first);
}

struct engine engine_bing = {
    .name = "bing",
    .shortcut = "bi",
    .website = "www.bing.com",
    .safesearch = 1,
    .bang = "https://www.bing.com/search?q=%s",
    .categories = {"general"},
    .paging = 1,
    .search = search,
};
