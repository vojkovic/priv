#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engines/engine.h"

#define BASE_URL "https://www.reuters.com"
#define PAGE_SIZE 20

//
// Reuters' own search api
//
// the query goes in as json inside the url (see search()). it is sorted by
// relevance, and the time filter is a start date.
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *articles, *r, *thumb;
	struct main_result *m;
	yyjson_doc *doc;
	const char *resizer;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	articles = yyjson_ptr_get(yyjson_doc_get_root(doc), "/result/articles");
	yyjson_arr_foreach (articles, i, n, r) {
		m = add_main(j, aprintf(j->a, BASE_URL "%s", json_str(r, "canonical_url")), json_str(r, "web"),
		    json_str(r, "description"));
		// the thumbnail is a resizer url, ask for a small one
		thumb = yyjson_obj_get(r, "thumbnail");
		if (*(resizer = json_str(thumb, "resizer_url")))
			m->thumbnail = aprintf(j->a, "%s&height=80", resizer);
		m->pubdate = (char *)format_date(j->a, json_str(r, "display_time"));
	}
	job_done(j);
}

static void
search(struct job *j)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_obj(d);
	struct buf q = {0};
	struct fetch *f;
	char start[32];
	time_t then;
	char *args;

	yyjson_mut_doc_set_root(d, root);
	yyjson_mut_obj_add_str(d, root, "keyword", j->query);
	yyjson_mut_obj_add_int(d, root, "offset", (j->pageno - 1) * PAGE_SIZE);
	yyjson_mut_obj_add_str(d, root, "orderby", "relevance");
	yyjson_mut_obj_add_int(d, root, "size", PAGE_SIZE);
	yyjson_mut_obj_add_str(d, root, "website", "reuters");
	if (j->filters->time_range) {
		then = time(NULL) - time_range_days(j->filters->time_range) * 86400;
		strftime(start, sizeof(start), "%Y-%m-%dT%H:%M:%S", localtime(&then));
		yyjson_mut_obj_add_str(d, root, "start_date", start);
	}
	args = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
	url_quote(&q, args, "", 1);
	free(args);
	f = job_request(j,
	    aprintf(j->a, BASE_URL "/pf/api/v3/content/fetch/articles-by-search-v2?query=%s", buf_str(&q)));
	buf_free(&q);
	yyjson_mut_doc_free(d);
	job_send(j, f, response);
}

struct engine engine_reuters = {
    .name = "reuters",
    .shortcut = "reu",
    .website = "www.reuters.com",
    .bang = "https://www.reuters.com/site-search/?query=%s",
    .category = "news",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
