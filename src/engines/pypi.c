#include "engines/engine.h"

static void
response(struct job *j, struct fetch *f)
{
	struct main_result *m;
	struct xnodes entries;
	htmlDocPtr doc;
	char *name, *created;
	int i;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	entries = xpath((xmlNodePtr)doc, "/html/body/main/div/div/div/form/div/ul/li/a[@class=\"package-snippet\"]");
	for (i = 0; i < entries.n; i++) {
		name = xpath_text(j->a, entries.v[i], "./h3/span[@class=\"package-snippet__name\"]");
		m = add_main(j, aprintf(j->a, "https://pypi.org%s", xpath_text(j->a, entries.v[i], "./@href")), name,
		    xpath_text(j->a, entries.v[i], "./p"));
		m->template = "packages";
		created =
		    xpath_text(j->a, entries.v[i], "./h3/span[@class=\"package-snippet__created\"]/time/@datetime");
		result_attr(j, m, "Package", name);
		result_attr(j, m, "Version",
		    xpath_text(j->a, entries.v[i], "./h3/span[@class=\"package-snippet__version\"]"));
		m->pubdate = (char *)format_date(j->a, created);
	}
	xnodes_free(&entries);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	url_quote(&q, j->query, "", 1);
	f = job_request(j, aprintf(j->a, "https://pypi.org/search/?q=%s&page=%d", buf_str(&q), j->pageno));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_pypi = {
    .name = "pypi",
    .shortcut = "pypi",
    .website = "pypi.org",
    .bang = "https://pypi.org/search/?q=%s",
    .category = "tech",
    .search = search,
};
