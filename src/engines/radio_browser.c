#include <string.h>

#include "engines/engine.h"

#define PAGE_SIZE 10

//
// Radio Browser, a community list of internet radio stations
//
// this is one of its public servers. the answer is a plain json list, most
// voted station first.
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *stations, *r;
	struct main_result *m;
	struct buf about = {0};
	const char *url, *part;
	size_t i, n, k;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len)) || !yyjson_is_arr(stations = yyjson_doc_get_root(doc))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (stations, i, n, r) {
		const char *parts[] = {json_str(r, "country"), json_str(r, "tags"), json_str(r, "codec")};
		// the station's site if it has one, else the stream itself
		url = *json_str(r, "homepage") ? json_str(r, "homepage") : json_str(r, "url_resolved");

		// the description is country, tags and codec, joined with dots
		about.len = 0;
		for (k = 0; k < LEN(parts); k++) {
			part = parts[k];
			if (*part)
				buf_printf(&about, "%s%s", about.len ? " \xc2\xb7 " : "", part);
		}
		m = add_main(j, url, json_str(r, "name"), buf_str(&about));
		if (m && *json_str(r, "favicon"))
			m->thumbnail = astrdup(j->a, json_str(r, "favicon"));
	}
	buf_free(&about);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "name", j->query);
	form_add(&q, "limit", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "offset", aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE));
	form_add(&q, "hidebroken", "true");
	form_add(&q, "order", "votes");
	form_add(&q, "reverse", "true");
	f = job_request(j, aprintf(j->a, "https://de1.api.radio-browser.info/json/stations/search?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_radio_browser = {
    .name = "radio browser",
    .shortcut = "rb",
    .website = "www.radio-browser.info",
    .bang = "https://www.radio-browser.info/search?name=%s",
    .category = "music",
    .paging = 1,
    .search = search,
};
