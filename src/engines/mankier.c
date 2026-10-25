#include "../engine.h"

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *results, *r;
	yyjson_doc *doc;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	results = yyjson_obj_get(yyjson_doc_get_root(doc), "results");
	yyjson_arr_foreach (results, i, n, r)
		add_main(j, json_str(r, "url"), json_str(r, "name"), json_str(r, "description"));
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	url_quote(&q, j->query, "", 1);
	f = job_request(j, aprintf(j->a, "https://www.mankier.com/api/v2/mans/?q=%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_mankier = {
    .name = "mankier",
    .shortcut = "man",
    .website = "www.mankier.com",
    .bang = "https://www.mankier.com/?q=%s",
    .category = "tech",
    .search = search,
};
