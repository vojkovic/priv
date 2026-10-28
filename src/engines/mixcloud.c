#include "engines/engine.h"

#define PAGE_SIZE 10

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *data, *r;
	struct main_result *m;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(data = yyjson_obj_get(yyjson_doc_get_root(doc), "data"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (data, i, n, r) {
		m = add_main(j, json_str(r, "url"), json_str(r, "name"), json_str(yyjson_obj_get(r, "user"), "name"));
		if (!m)
			continue;
		m->thumbnail = astrdup(j->a, json_str(yyjson_obj_get(r, "pictures"), "medium"));
		m->pubdate = (char *)format_date(j->a, json_str(r, "created_time"));
		m->length = format_duration(j->a, (long)yyjson_get_sint(yyjson_obj_get(r, "audio_length")));
	}
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "type", "cloudcast");
	form_add(&q, "limit", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "offset", aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE));
	f = job_request(j, aprintf(j->a, "https://api.mixcloud.com/search/?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_mixcloud = {
    .name = "mixcloud",
    .shortcut = "mc",
    .website = "www.mixcloud.com",
    .bang = "https://www.mixcloud.com/search/?q=%s",
    .category = "music",
    .paging = 1,
    .search = search,
};
