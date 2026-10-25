#include <string.h>

#include "../engine.h"

#define PAGE_SIZE 10

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *pages, *item, *info, *key;
	struct main_result *m;
	yyjson_doc *doc;
	char *title, *dot;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	pages = yyjson_ptr_get(yyjson_doc_get_root(doc), "/query/pages");
	yyjson_obj_foreach (pages, i, n, key, item) {
		if (!(info = yyjson_arr_get_first(yyjson_obj_get(item, "imageinfo"))))
			continue;
		title = astrdup(j->a, json_str(item, "title"));
		if (starts_with(title, "File:"))
			title += 5;
		if ((dot = strrchr(title, '.')))
			*dot = 0;
		m = add_main(j, json_str(info, "descriptionurl"), title, html_to_text(j->a, json_str(item, "snippet")));
		m->length = format_duration(j->a, (long)yyjson_get_num(yyjson_obj_get(info, "duration")));
	}
	job_done(j);
}

static void
search(struct job *j)
{
	const char *tag = j->filters->locale->tag;
	struct buf q = {0};
	struct fetch *f;

	buf_puts(&q, "format=json&uselang=");
	buf_puts(&q, strcmp(tag, "all") ? astrndup(j->a, tag, strcspn(tag, "-")) : "en");
	buf_printf(&q,
	    "&action=query&prop=info|imageinfo&generator=search&gsrnamespace=6&gsrprop=snippet"
	    "&gsrlimit=%d&gsroffset=%d&gsrsearch=",
	    PAGE_SIZE, PAGE_SIZE * (j->pageno - 1));
	url_quote(&q, aprintf(j->a, "filetype:audio %s", j->query), ":|", 1);
	buf_puts(&q, "&iiprop=url|size|mime&iiurlheight=180");
	f = job_request(j, aprintf(j->a, "https://commons.wikimedia.org/w/api.php?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_wikicommons_audio = {
    .name = "wikicommons.audio",
    .shortcut = "wca",
    .website = "commons.wikimedia.org",
    .bang = "https://commons.wikimedia.org/w/index.php?search=%s&fulltext=1",
    .category = "music",
    .paging = 1,
    .search = search,
};
