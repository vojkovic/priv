#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../engine.h"
#include "../token.h"

#define PAGE_SIZE 20

static int
range_days(const char *r)
{
	return !strcmp(r, "day") ? 1 : !strcmp(r, "week") ? 7 : !strcmp(r, "month") ? 30 : 365;
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *hits, *r;
	struct main_result *m;
	yyjson_doc *doc;
	const char *thumb, *size;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(hits = yyjson_obj_get(yyjson_doc_get_root(doc), "hits"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (hits, i, n, r) {
		m = add_main(j, json_str(r, "canonicalURL"), json_str(r, "title"), json_str(r, "synopsis"));
		thumb = json_str(yyjson_ptr_get(r, "/defaultThumbnail"), "16x9");
		if (*thumb && (size = strstr(thumb, "&width=")))
			m->thumbnail = aprintf(j->a, "%.*s&width=240&height=135", (int)(size - thumb), thumb);
		m->pubdate = (char *)format_date(j->a, json_str(yyjson_ptr_get(r, "/dates"), "liveDate"));
	}
	job_done(j);
}

static void
request(struct job *j, const char *key)
{
	struct buf q = {0};
	struct fetch *f;
	char *filters = astrdup(j->a, "contentType:audio OR contentType:audioepisode OR contentType:audiosegment");

	if (j->filters->time_range)
		filters = aprintf(j->a, "(%s) AND unixDates.liveDate>=%lld", filters,
		    (long long)time(NULL) - range_days(j->filters->time_range) * 86400LL);
	form_add(&q, "query", j->query);
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno - 1));
	form_add(&q, "hitsPerPage", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "filters", filters);
	form_add(&q, "attributesToRetrieve", "title,canonicalURL,synopsis,dates.liveDate,defaultThumbnail.16x9");
	form_add(&q, "attributesToHighlight", "");
	f = job_request(j, aprintf(j->a, "https://MR0JFLPBAR-dsn.algolia.net/1/indexes/news_prod_all?%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "x-algolia-application-id: MR0JFLPBAR");
	fetch_header(f, "x-algolia-api-key: %s", key);
	job_send(j, f, response);
}

static void
token_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	const char *key;
	size_t n;

	if (f->code != CURLE_OK)
		token_fail(t, "network error", SUSPEND_DEFAULT);
	else if (f->status >= 400 || !(key = find_between(fetch_body(f), "\"apiKey\":\"", "\"", &n)) || !n)
		token_fail(t, "server API error", SUSPEND_NONE);
	else {
		char *v = xmalloc(n + 1);
		memcpy(v, key, n);
		v[n] = 0;
		token_set(t, v);
		free(v);
	}
	fetch_free(f);
}

static void
token_fetch(struct token *t)
{
	struct fetch *f = fetch_new("https://www.abc.net.au/news/search");

	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, token_response, t);
}

static struct token token = {
    .name = "abc radio",
    .ttl = 86400,
    .refresh = 80000,
    .fetch = token_fetch,
};

static void
init(void)
{
	token_warm(&token);
}

static void
search(struct job *j)
{
	token_get(j, &token, request);
}

struct engine engine_abc_radio = {
    .name = "abc radio",
    .shortcut = "abcr",
    .website = "www.abc.net.au",
    .bang = "https://www.abc.net.au/listen/search?q=%s",
    .category = "music",
    .paging = 1,
    .time_range = 1,
    .init = init,
    .search = search,
};
