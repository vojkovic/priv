#include <string.h>

#include "../engine.h"

#define PAGE_SIZE 10

static const char *
first(yyjson_val *obj, const char *key)
{
	const char *s = yyjson_get_str(yyjson_arr_get(yyjson_obj_get(obj, key), 0));

	return s ? s : "";
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *items, *r, *authors, *a, *parts;
	struct main_result *m;
	struct buf names = {0};
	size_t i, n, k, c;
	long y, mo, d;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(items = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "message"), "items"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (items, i, n, r) {
		if (!*first(r, "title"))
			continue;
		m = add_paper(j, json_str(r, "URL"), first(r, "title"), html_to_text(j->a, json_str(r, "abstract")));
		if (!m)
			continue;
		parts = yyjson_arr_get(yyjson_obj_get(yyjson_obj_get(r, "published"), "date-parts"), 0);
		y = (long)yyjson_get_sint(yyjson_arr_get(parts, 0));
		mo = (long)yyjson_get_sint(yyjson_arr_get(parts, 1));
		d = (long)yyjson_get_sint(yyjson_arr_get(parts, 2));
		if (y)
			m->pubdate =
			    (char *)format_date(j->a, aprintf(j->a, "%04ld-%02ld-%02ld", y, mo ? mo : 1, d ? d : 1));
		names.len = 0;
		authors = yyjson_obj_get(r, "author");
		yyjson_arr_foreach (authors, k, c, a)
			if (k < 6)
				buf_printf(&names, "%s%s %s", names.len ? ", " : "", json_str(a, "given"),
				    json_str(a, "family"));
		result_attr(j, m, "Author", astrdup(j->a, buf_str(&names)));
		result_attr(j, m, "Journal", first(r, "container-title"));
		result_attr(j, m, "DOI", json_str(r, "DOI"));
	}
	buf_free(&names);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "query", j->query);
	form_add(&q, "rows", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "offset", aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE));
	f = job_request(j, aprintf(j->a, "https://api.crossref.org/works?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_crossref = {
    .name = "crossref",
    .shortcut = "cr",
    .website = "www.crossref.org",
    .bang = "https://search.crossref.org/?q=%s",
    .category = "research",
    .paging = 1,
    .search = search,
};
