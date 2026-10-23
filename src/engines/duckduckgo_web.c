#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../engine.h"

#define BASE_URL "https://duckduckgo.com"

static const char *
page_key(struct job *j, int pageno)
{
	return aprintf(j->a, "duckduckgo web nextpage_url|%s|%d", j->query, pageno);
}

static void
api_headers(struct fetch *f)
{
	fetch_impersonate(f, impersonate_firefox, 0);
	fetch_header(f, "Accept: */*");
	fetch_header(f, "Sec-Fetch-Dest: script");
	fetch_header(f, "Sec-Fetch-Mode: no-cors");
	fetch_header(f, "Sec-Fetch-Site: same-site");
	fetch_header(f, "Referer: " BASE_URL "/");
}

static int
parse_results(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *results, *r, *last = NULL;
	const char *next;
	size_t i, n;

	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(results = yyjson_obj_get(yyjson_doc_get_root(doc), "results")))
		return -1;
	yyjson_arr_foreach (results, i, n, r) {
		const char *url = yyjson_get_str(yyjson_obj_get(r, "u"));
		last = r;
		if (!url || !*url)
			continue;
		add_main(j, url, html_to_text(j->a, json_str(r, "t")), html_to_text(j->a, json_str(r, "a")));
	}
	if (last && *(next = json_str(last, "n")))
		cache_set(page_key(j, j->pageno + 1), aprintf(j->a, BASE_URL "%s", next), 3600);
	return 0;
}

static void
followed(struct job *j, struct fetch *f)
{
	if (!job_ok(j, f))
		return;
	if (parse_results(j, f) < 0) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	job_done(j);
}

static int
html_len(const char *s, size_t n)
{
	static const struct {
		const char *k;
		int v;
	} map[] = {
	    {"<p><div></p><p></div", 32},
	    {"<li><div></li><li></div", 29},
	    {"<div><div></div><div></div", 33},
	    {"<br><div></br><br></div", 23},
	};
	size_t i;

	for (i = 0; i < LEN(map); i++)
		if (strlen(map[i].k) == n && !memcmp(map[i].k, s, n))
			return map[i].v;
	return -1;
}

static const char *
find_function(const char *js, const char *name, size_t *len)
{
	const char *p = js, *body, *end;
	size_t n = strlen(name);

	while ((p = strstr(p, "let "))) {
		p += 4;
		if (strncmp(p, name, n) || strncmp(p + n, " = function(num) {", 18))
			continue;
		body = p + n + 18;
		end = strchr(body, '}');
		if (!end || strncmp(end, "};", 2))
			continue;
		*len = end - body;
		return body;
	}
	return NULL;
}

static char *
solve_jsa(struct job *j, const char *js)
{
	const char *p, *path, *call, *body, *e, *m, *bt, *bt2;
	int hl;
	char name[64];
	long long jsa;
	size_t n, plen, blen;

	if (!(p = strstr(js, "let jsa = ")) || !IS_DIGIT(p[10]))
		return NULL;
	jsa = strtoll(p + 10, (char **)&e, 10);
	if (*e != ';' || !(path = strstr(e, "DDG.deep.initialize('")))
		return NULL;
	path += 21;
	if (!(e = strchr(path, '\'')))
		return NULL;
	plen = e - path;
	for (call = js; (call = strstr(call, "jsa = "));) {
		call += 6;
		for (n = 0; n < sizeof(name) - 1 && (IS_ALNUM(call[n]) || call[n] == '_'); n++)
			name[n] = call[n];
		name[n] = 0;
		if (!n || strncmp(call + n, "(jsa);", 6))
			continue;
		if (!(body = find_function(js, name, &blen)))
			return NULL;
		m = memmem(body, blen, "num * ", 6);
		if (m && IS_DIGIT(m[6])) {
			jsa *= strtoll(m + 6, NULL, 10);
		} else {
			if (!(bt = memchr(body, '`', blen)) || !(bt2 = memchr(bt + 1, '`', body + blen - bt - 1)) ||
			    (hl = html_len(bt + 1, bt2 - bt - 1)) < 0)
				return NULL;
			jsa += hl;
		}
	}
	return url_join(j->a, "https://links.duckduckgo.com", aprintf(j->a, "%.*s%lld", (int)plen, path, jsa));
}

static void
response(struct job *j, struct fetch *f)
{
	struct fetch *nf;
	char *url;

	if (!job_http_ok(j, f))
		return;
	if (strstr(fetch_body(f), "let jsa =") && (url = solve_jsa(j, fetch_body(f)))) {
		nf = job_get(j, url);
		if (!j->e->no_accept_language)
			fetch_header(nf, "Accept-Language: en-US,en;q=0.9");
		api_headers(nf);
		job_send(j, nf, followed);
		return;
	}
	if (parse_results(j, f) < 0) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	job_done(j);
}

static void
request(struct job *j, const char *api_url)
{
	struct fetch *f;
	const char *p;

	if ((p = strstr(api_url, "/d.js?")))
		api_url = aprintf(j->a, "%.*s/d.js?o=json&%s", (int)(p - api_url), api_url, p + 6);
	f = job_request(j, api_url);
	api_headers(f);
	job_send(j, f, response);
}

static void
first_page(struct job *j, struct fetch *f)
{
	htmlDocPtr doc;
	char *link;

	if (!job_ok(j, f))
		return;
	if (!(doc = job_html(j, f)) ||
	    !*(link = xpath_text(j->a, (xmlNodePtr)doc, "//link[@id='deep_preload_link']/@href"))) {
		job_done(j);
		return;
	}
	cache_set(page_key(j, 1), link, 7200);
	request(j, link);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;
	const char *api;

	if (strlen(j->query) >= 500) {
		job_done(j);
		return;
	}
	if ((api = cache_get(page_key(j, j->pageno)))) {
		request(j, astrdup(j->a, api));
		return;
	}
	if (j->pageno > 1) {
		job_done(j);
		return;
	}
	url_quote(&q, j->query, "", 1);
	f = job_get(j, aprintf(j->a, BASE_URL "/?q=%s&t=h_&ia=web", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "Accept-Language: en-US,en;q=0.9");
	fetch_impersonate(f, impersonate_firefox, 0);
	job_send(j, f, first_page);
}

struct engine engine_duckduckgo_web = {
    .name = "duckduckgo web",
    .shortcut = "ddgw",
    .website = "duckduckgo.com",
    .bang = "https://html.duckduckgo.com/html/?q=%s",
    .categories = {"general"},
    .paging = 1,
    .search = search,
};
