#include <string.h>

#include "../engine.h"

#define BASE_URL "https://www.bing.com"

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes items;
	struct main_result *m;
	htmlDocPtr doc;
	char *thumb;
	int i;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	items = xpath((xmlNodePtr)doc, "//div[contains(@class, 'newsitem')]");
	for (i = 0; i < items.n; i++) {
		m = add_main(j, xpath_first_text(j->a, items.v[i], ".//a[@class='title']/@href"),
		    xpath_text(j->a, items.v[i], ".//a[@class='title']"),
		    xpath_text(j->a, items.v[i], ".//div[@class='snippet']"));
		if (!m)
			continue;
		m->pubdate = xpath_first_text(j->a, items.v[i],
		    ".//div[contains(@class, 'source')]//span[@aria-label]/@aria-label");
		thumb = xpath_first_text(j->a, items.v[i], ".//a[@class='imagelink']//img/@src");
		if (thumb && *thumb)
			m->thumbnail = starts_with(thumb, "http") ?
			    thumb :
			    aprintf(j->a, BASE_URL "%s%s", *thumb == '/' ? "" : "/", thumb);
	}
	xnodes_free(&items);
	job_done(j);
}

static void
search(struct job *j)
{
	const char *range = time_range_code(j->filters, "7", "8", "9", NULL);
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "InfiniteScroll", "1");
	form_add(&q, "first", aprintf(j->a, "%d", (j->pageno - 1) * 10 + 1));
	form_add(&q, "SFX", aprintf(j->a, "%d", j->pageno - 1));
	form_add(&q, "form", "PTFTNR");
	if (range)
		form_add(&q, "qft", aprintf(j->a, "interval=\"%s\"", range));
	f = job_request(j, aprintf(j->a, BASE_URL "/news/infinitescrollajax?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_bing_news = {
    .name = "bing news",
    .shortcut = "bin",
    .website = "www.bing.com",
    .bang = "https://www.bing.com/news/search?q=%s",
    .category = "news",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
