#include <string.h>

#include "engines/engine.h"

#define BASE_URL "https://oceanhero.today"

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *pages, *r;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	pages = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "webPages"), "value");
	yyjson_arr_foreach (pages, i, n, r)
		add_main(j, json_str(r, "url"), json_str(r, "name"), json_str(r, "snippet"));
	job_done(j);
}

static void
search(struct job *j)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
	const char *tag = j->filters->locale->tag;
	char id[22];
	struct buf q = {0};
	struct fetch *f;
	size_t i;

	/* the api only wants a user id shaped like the site's own, so every search gets a new one */
	for (i = 0; i < sizeof(id) - 1; i++)
		id[i] = alphabet[rnd(sizeof(alphabet) - 1)];
	id[i] = 0;
	form_add(&q, "q", j->query);
	form_add(&q, "mkt", strchr(tag, '-') ? tag : "en-US");
	form_add(&q, "safeSearch", (const char *[]){"Off", "Moderate", "Strict"}[j->filters->safesearch]);
	form_add(&q, "personalizedResults", "no");
	f = job_request(j, aprintf(j->a, BASE_URL "/api/search?%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "Accept: application/json");
	fetch_header(f, "Referer: " BASE_URL "/web");
	fetch_cookie(f, "OH_USER_ID", id);
	job_send(j, f, response);
}

struct engine engine_oceanhero = {
    .name = "oceanhero",
    .shortcut = "oh",
    .website = "oceanhero.today",
    .safesearch = 1,
    .bang = "https://oceanhero.today/web?q=%s",
    .category = "general",
    .search = search,
};
