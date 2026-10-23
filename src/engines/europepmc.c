#include <string.h>

#include "../engine.h"

static const char *
cursor_key(struct job *j, int pageno)
{
	return aprintf(j->a, "europepmc cursor|%s|%d", j->query, pageno);
}

static const char *
authors(struct job *j, const char *list)
{
	char *s = astrdup(j->a, list), *a, *save, *end;
	struct buf b = {0};
	const char *out;

	for (a = strtok_r(s, ",", &save); a; a = strtok_r(NULL, ",", &save)) {
		a = str_strip(a);
		end = a + strlen(a);
		while (end > a && end[-1] == '.')
			*--end = 0;
		if (*a)
			buf_printf(&b, "%s%s", b.len ? ", " : "", a);
	}
	out = astrdup(j->a, buf_str(&b));
	buf_free(&b);
	return out;
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *root, *list, *item, *journal, *types, *t, *urls, *u;
	struct main_result *m;
	struct buf tb = {0};
	const char *source, *id, *next, *url;
	yyjson_doc *doc;
	size_t i, n, k, kn;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	root = yyjson_doc_get_root(doc);
	if (*(next = json_str(root, "nextCursorMark")))
		cache_set(cursor_key(j, j->pageno + 1), next, 3600);
	list = yyjson_ptr_get(root, "/resultList/result");
	yyjson_arr_foreach (list, i, n, item) {
		source = json_str(item, "source");
		id = json_str(item, "id");
		url = *source && *id ? aprintf(j->a, "https://europepmc.org/article/%s/%s", source, id) : "";
		m = add_paper(j, url, html_to_text(j->a, json_str(item, "title")),
		    html_to_text(j->a, json_str(item, "abstractText")));
		m->pubdate = (char *)format_date(j->a, json_str(item, "firstPublicationDate"));
		result_attr(j, m, "Author", authors(j, json_str(item, "authorString")));
		journal = yyjson_ptr_get(item, "/journalInfo/journal");
		result_attr(j, m, "Journal", json_str(journal, "title"));
		buf_free(&tb);
		types = yyjson_ptr_get(item, "/pubTypeList/pubType");
		yyjson_arr_foreach (types, k, kn, t)
			buf_printf(&tb, "%s%s", k ? ", " : "", yyjson_get_str(t) ? yyjson_get_str(t) : "");
		result_attr(j, m, "Type", buf_str(&tb));
		result_attr(j, m, "DOI", json_str(item, "doi"));
		result_attr(j, m, "ISSN", json_str(journal, "issn"));
		urls = yyjson_ptr_get(item, "/fullTextUrlList/fullTextUrl");
		yyjson_arr_foreach (urls, k, kn, u) {
			if (!strcmp(json_str(u, "documentStyle"), "pdf") &&
			    !strcmp(json_str(u, "availabilityCode"), "OA")) {
				m->pdf_url = astrdup(j->a, json_str(u, "url"));
				break;
			}
		}
	}
	buf_free(&tb);
	job_done(j);
}

static void
search(struct job *j)
{
	const char *cursor = NULL;
	struct buf q = {0};
	struct fetch *f;

	if (j->pageno > 1 && !(cursor = cache_get(cursor_key(j, j->pageno)))) {
		job_done(j);
		return;
	}
	form_add(&q, "query", j->query);
	form_add(&q, "format", "json");
	form_add(&q, "resultType", "core");
	form_add(&q, "pageSize", "20");
	if (cursor)
		form_add(&q, "cursorMark", cursor);
	f = job_request(j, aprintf(j->a, "https://www.ebi.ac.uk/europepmc/webservices/rest/search?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_europepmc = {
    .name = "europepmc",
    .shortcut = "epmc",
    .website = "europepmc.org",
    .bang = "https://europepmc.org/search?query=%s",
    .categories = {"research"},
    .paging = 1,
    .search = search,
};
