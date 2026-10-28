#include "engines/engine.h"

//
// MDN's own search api, plain json
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *docs, *r;
	yyjson_doc *doc;
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
	docs = yyjson_obj_get(yyjson_doc_get_root(doc), "documents");
	yyjson_arr_foreach (docs, i, n, r)
		add_main(j, aprintf(j->a, "https://developer.mozilla.org%s", json_str(r, "mdn_url")),
		    json_str(r, "title"), json_str(r, "summary"));
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	url_quote(&q, j->query, "", 1);
	f = job_request(j,
	    aprintf(j->a, "https://developer.mozilla.org/api/v1/search?q=%s&page=%d", buf_str(&q), j->pageno));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_mdn = {
    .name = "mdn",
    .shortcut = "mdn",
    .website = "developer.mozilla.org",
    .bang = "https://developer.mozilla.org/en-US/search?q=%s",
    .category = "tech",
    .paging = 1,
    .search = search,
};
