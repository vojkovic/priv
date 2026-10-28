#include <stdlib.h>
#include <string.h>

#include "engines/engine.h"

#define BASE_URL "https://www.bing.com"
#define PAGE_SIZE 35

//
// Bing videos, from the page's async endpoint
//
// answers with just the video tiles as html. each tile has its details in a
// json attribute, see response().
//

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes items;
	struct main_result *m;
	htmlDocPtr doc;
	yyjson_doc *meta;
	yyjson_val *root;
	const char *raw;
	int i;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	items = xpath((xmlNodePtr)doc, "//div[contains(@id, 'mc_vtvc_video')]");
	for (i = 0; i < items.n; i++) {
		/* each video carries its details as json in vrhm: murl the page, vt the title, du the length */
		raw = xpath_first_text(j->a, items.v[i], ".//div[@class='vrhdata']/@vrhm");
		if (!raw || !(meta = job_json(j, raw, strlen(raw))))
			continue; // no details, skip the tile
		root = yyjson_doc_get_root(meta);
		m = add_video(j, json_str(root, "murl"), json_str(root, "vt"),
		    xpath_text(j->a, items.v[i], ".//div[@class='mc_vtvc_meta_block']//span"));
		if (!m)
			continue;
		m->length = astrdup(j->a, json_str(root, "du"));
		m->thumbnail = xpath_first_text(j->a, items.v[i], ".//img[starts-with(@class, 'rms')]/@data-src-hq");
	}
	xnodes_free(&items);
	job_done(j);
}

static void
search(struct job *j)
{
	const char *age = time_range_code(j->filters, "1440", "10080", "43200", "525600");
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "async", "content");
	form_add(&q, "first", aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE + 1));
	form_add(&q, "count", aprintf(j->a, "%d", PAGE_SIZE));
	// the time filter is "newer than this many minutes"
	if (age) {
		form_add(&q, "form", "VRFLTR");
		form_add(&q, "qft", aprintf(j->a, " filterui:videoage-lt%s", age));
	}
	f = job_request(j, aprintf(j->a, BASE_URL "/videos/asyncv2?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_bing_videos = {
    .name = "bing videos",
    .shortcut = "biv",
    .website = "www.bing.com",
    .bang = "https://www.bing.com/videos/search?q=%s",
    .category = "videos",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
