#include "engines/engine.h"

#define BASE_URL "https://search.answers.com"

//
// S1search (the answers.com search)
//
// the page prints its results as a javascript object, "response: {...} };",
// so we cut that out of the html and read it as json. no html parsing.
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *regions, *region, *groups, *group, *results, *r;
	const char *data, *url;
	size_t n, i, k, x, y, m, c;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(data = find_between(fetch_body(f), "response: ", " };", &n)) || !(doc = job_json(j, data, n))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	regions = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "search"), "regions");
	yyjson_arr_foreach (regions, i, k, region) {
		// mainline is the results column, the rest is ads and sidebars
		if (strcmp(json_str(region, "name"), "mainline"))
			continue;
		groups = yyjson_obj_get(region, "groups");
		yyjson_arr_foreach (groups, x, y, group) {
			if (strcmp(json_str(group, "result_type"), "web"))
				continue;
			results = yyjson_obj_get(group, "results");
			yyjson_arr_foreach (results, m, c, r) {
				// some results only have the tracking link
				url = json_str(r, "url");
				add_main(j, *url ? url : json_str(r, "clickUrl"),
				    html_to_text(j->a, json_str(r, "title")),
				    html_to_text(j->a, json_str(r, "description")));
			}
		}
	}
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno));
	f = job_request(j, aprintf(j->a, BASE_URL "/?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_s1search = {
    .name = "s1search",
    .shortcut = "s1",
    .website = "search.answers.com",
    .category = "general",
    .paging = 1,
    .search = search,
};
