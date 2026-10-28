#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engines/engine.h"
#include "engines/tokens.h"

#define PAGE_SIZE 20

//
// ABC search is algolia
//
// the api key is public and sits in the search page, so tokens.c reads it
// from there and caches it for a day, shared with the other abc engine. abc
// rotates it now and then, when that happens the engines error until the
// next refresh picks up the new one.
//

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

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(hits = yyjson_obj_get(yyjson_doc_get_root(doc), "hits"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (hits, i, n, r) {
		m = add_main(j, json_str(r, "canonicalURL"), json_str(r, "title"), json_str(r, "synopsis"));

		// thumbnails come at 862 wide, ask for a size we would actually show
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

	// the index is mixed. audio is what radio and podcasts are filed under
	if (j->filters->time_range)
		filters = aprintf(j->a, "(%s) AND unixDates.liveDate>=%lld", filters,
		    (long long)time(NULL) - time_range_days(j->filters->time_range) * 86400LL);
	form_add(&q, "query", j->query);
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno - 1));
	form_add(&q, "hitsPerPage", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "filters", filters);

	// ask for just what we show, 20 hits drop from ~112 KB to ~14 KB
	form_add(&q, "attributesToRetrieve", "title,canonicalURL,synopsis,dates.liveDate,defaultThumbnail.16x9");
	form_add(&q, "attributesToHighlight", "");
	f = job_request(j, aprintf(j->a, "https://MR0JFLPBAR-dsn.algolia.net/1/indexes/news_prod_all?%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "x-algolia-application-id: MR0JFLPBAR");
	fetch_header(f, "x-algolia-api-key: %s", key);
	job_send(j, f, response);
}

static void
init(void)
{
	token_warm(&abc_token);
}

static void
search(struct job *j)
{
	token_get(j, &abc_token, request);
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
