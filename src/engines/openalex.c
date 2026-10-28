#include <stdlib.h>
#include <string.h>

#include "engines/engine.h"

#define PAGE_SIZE 10
#define MAX_WORDS 400

//
// OpenAlex, plain json
//
// abstracts are the odd part: they come as an index from each word to the
// places it appears, not as text, so abstract() puts them back in order.
//

/* abstracts come as an inverted index, {"word": [positions]}, so they are put back in order */
static const char *
abstract(struct job *j, yyjson_val *index)
{
	const char *words[MAX_WORDS] = {0};
	yyjson_val *key, *positions, *pos;
	yyjson_obj_iter it;
	struct buf b = {0};
	size_t i, n, last = 0;
	long p;
	const char *out;

	if (!yyjson_is_obj(index))
		return "";
	yyjson_obj_iter_init(index, &it);
	while ((key = yyjson_obj_iter_next(&it))) {
		positions = yyjson_obj_iter_get_val(key);
		yyjson_arr_foreach (positions, i, n, pos) {
			p = (long)yyjson_get_sint(pos);
			if (p >= 0 && p < MAX_WORDS) {
				words[p] = yyjson_get_str(key);
				if ((size_t)p >= last)
					last = (size_t)p + 1;
			}
		}
	}
	for (i = 0; i < last; i++)
		if (words[i])
			buf_printf(&b, "%s%s", b.len ? " " : "", words[i]);
	out = astrdup(j->a, buf_str(&b));
	buf_free(&b);
	return out;
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *works, *r, *authors, *a, *source;
	struct main_result *m;
	struct buf names = {0};
	const char *url, *doi;
	size_t i, n, k, c;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(works = yyjson_obj_get(yyjson_doc_get_root(doc), "results"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (works, i, n, r) {
		// the doi is a link that goes to the paper, use it when there is one
		doi = json_str(r, "doi");
		url = *doi ? doi : json_str(r, "id");
		m = add_paper(j, url, json_str(r, "display_name"),
		    abstract(j, yyjson_obj_get(r, "abstract_inverted_index")));
		if (!m)
			continue;
		m->pubdate = (char *)format_date(j->a, json_str(r, "publication_date"));
		// the first six authors
		names.len = 0;
		authors = yyjson_obj_get(r, "authorships");
		yyjson_arr_foreach (authors, k, c, a)
			if (k < 6)
				buf_printf(&names, "%s%s", names.len ? ", " : "",
				    json_str(yyjson_obj_get(a, "author"), "display_name"));
		result_attr(j, m, "Author", astrdup(j->a, buf_str(&names)));
		source = yyjson_obj_get(yyjson_obj_get(r, "primary_location"), "source");
		result_attr(j, m, "Journal", json_str(source, "display_name"));
		if (*doi)
			result_attr(j, m, "DOI",
			    starts_with(doi, "https://doi.org/") ? doi + strlen("https://doi.org/") : doi);
		if (*json_str(yyjson_obj_get(r, "best_oa_location"), "pdf_url"))
			m->pdf_url = astrdup(j->a, json_str(yyjson_obj_get(r, "best_oa_location"), "pdf_url"));
	}
	buf_free(&names);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "search", j->query);
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno));
	form_add(&q, "per-page", aprintf(j->a, "%d", PAGE_SIZE));
	/* only the fields used here, a third of the size, keeps it inside the search timeout */
	form_add(&q, "select",
	    "id,doi,display_name,publication_date,authorships,primary_location,best_oa_location,abstract_inverted_index");
	f = job_request(j, aprintf(j->a, "https://api.openalex.org/works?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_openalex = {
    .name = "openalex",
    .shortcut = "oa",
    .website = "openalex.org",
    .bang = "https://openalex.org/works?search=%s",
    .category = "research",
    .paging = 1,
    .search = search,
};
