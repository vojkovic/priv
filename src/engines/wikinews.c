#include <string.h>

#include "../engine.h"

#define BASE_URL "https://en.wikinews.org"
#define PAGE_SIZE 10

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *hits, *r;
	struct main_result *m;
	char *title, *p;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(hits = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "query"), "search"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (hits, i, n, r) {
		title = astrdup(j->a, json_str(r, "title"));
		for (p = title; *p; p++)
			if (*p == ' ')
				*p = '_';
		m = add_main(j, aprintf(j->a, BASE_URL "/wiki/%s", title), json_str(r, "title"),
		    html_to_text(j->a, json_str(r, "snippet")));
		if (m)
			m->pubdate = (char *)format_date(j->a, json_str(r, "timestamp"));
	}
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "action", "query");
	form_add(&q, "list", "search");
	form_add(&q, "format", "json");
	form_add(&q, "srsearch", j->query);
	form_add(&q, "srprop", "snippet|timestamp");
	form_add(&q, "srlimit", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "sroffset", aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE));
	f = job_request(j, aprintf(j->a, BASE_URL "/w/api.php?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_wikinews = {
    .name = "wikinews",
    .shortcut = "wn",
    .website = "en.wikinews.org",
    .bang = "https://en.wikinews.org/w/index.php?search=%s",
    .categories = {"news"},
    .paging = 1,
    .search = search,
};
