#include "engines/engine.h"

#define BASE_URL "https://api.iseek.com"
#define PAGE_SIZE 10

//
// Iseek's search api
//
// the answers are groups of documents (solr style), and the text of a result
// can come as one string or as a list of pieces, see response().
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *groups, *g, *docs, *r, *content, *part;
	size_t i, n, k, m, x, y;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(groups = yyjson_obj_get(yyjson_doc_get_root(doc), "data"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (groups, i, n, g) {
		docs = yyjson_obj_get(yyjson_obj_get(g, "doclist"), "docs");
		yyjson_arr_foreach (docs, k, m, r) {
			struct buf text = {0};
			// the text is a string or a list of strings, join either
			content = yyjson_obj_get(r, "content_txt");
			if (yyjson_is_str(content))
				buf_puts(&text, yyjson_get_str(content));
			yyjson_arr_foreach (content, x, y, part)
				if (yyjson_is_str(part))
					buf_puts(&text, yyjson_get_str(part));
			add_main(j, json_str(r, "url_s"), json_str(r, "title_t"), astrdup(j->a, buf_str(&text)));
			buf_free(&text);
		}
	}
	job_done(j);
}

static void
search(struct job *j)
{
	unsigned char hash[32];
	struct sha256 s;
	struct buf q = {0};
	struct fetch *f;

	/* a token in the site's own format makes the api answer subsequent pages much faster */
	sha256_init(&s);
	sha256_update(&s, j->query, strlen(j->query));
	sha256_final(&s, hash);
	form_add(&q, "q", j->query);
	form_add(&q, "key", "core-web");
	form_add(&q, "num", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "off", aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE));
	form_add(&q, "rSort", "__metasearch_score_d:desc");
	form_add(&q, "names", "title_t,content_txt,url_s");
	form_add(&q, "qNames", "title_t");
	form_add(&q, "qToken", aprintf(j->a, "%.4s_%d", base64(j->a, hash, sizeof(hash)), j->pageno));
	f = job_request(j, aprintf(j->a, BASE_URL "/search?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_iseek = {
    .name = "iseek",
    .shortcut = "isk",
    .website = "www.iseek.com",
    .category = "general",
    .paging = 1,
    .max_page = 2,
    .search = search,
};
