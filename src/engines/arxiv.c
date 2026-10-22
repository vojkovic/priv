#include "../engine.h"

#define PAGE_SIZE 10

static char *
field(struct job *j, xmlNodePtr entry, const char *name)
{
	return xpath_text(j->a, entry, aprintf(j->a, "./*[local-name()=\"%s\"]", name));
}

static char *
joined(struct job *j, xmlNodePtr entry, const char *expr)
{
	struct xnodes nodes = xpath(entry, expr);
	struct buf b = {0};
	char *out;
	int i;

	for (i = 0; i < nodes.n; i++)
		buf_printf(&b, "%s%s", i ? ", " : "", node_text(j->a, nodes.v[i]));
	xnodes_free(&nodes);
	out = astrdup(j->a, buf_str(&b));
	buf_free(&b);
	return out;
}

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes entries;
	struct main_result *m;
	xmlDocPtr doc;
	xmlNodePtr e;
	int i;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_xml(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	entries = xpath((xmlNodePtr)doc, "//*[local-name()=\"entry\"]");
	for (i = 0; i < entries.n; i++) {
		e = entries.v[i];
		m = add_paper(j, field(j, e, "id"), field(j, e, "title"), field(j, e, "summary"));
		m->pubdate = (char *)format_date(j->a, field(j, e, "published"));
		result_attr(j, m, "Author", joined(j, e, "./*[local-name()=\"author\"]/*[local-name()=\"name\"]"));
		result_attr(j, m, "Journal", field(j, e, "journal_ref"));
		result_attr(j, m, "Tags", joined(j, e, "./*[local-name()=\"category\"]/@term"));
		result_attr(j, m, "DOI", field(j, e, "doi"));
		result_attr(j, m, "Comments", field(j, e, "comment"));
		m->pdf_url = xpath_first_text(j->a, e, "./*[local-name()=\"link\" and @title=\"pdf\"]/@href");
	}
	xnodes_free(&entries);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;
	char start[16];

	snprintf(start, sizeof(start), "%d", (j->pageno - 1) * PAGE_SIZE);
	form_add(&q, "search_query", aprintf(j->a, "all:%s", j->query));
	form_add(&q, "start", start);
	form_add(&q, "max_results", "10");
	f = job_request(j, aprintf(j->a, "https://export.arxiv.org/api/query?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_arxiv = {
    .name = "arxiv",
    .shortcut = "arx",
    .website = "arxiv.org",
    .categories = {"research"},
    .paging = 1,
    .search = search,
};
