#include <string.h>

#include "engines/engine.h"

//
// The Stack Exchange sites (stack overflow, ask ubuntu, super user)
//
// they all share one api and one answer, so this file is the whole engine
// and each site is only a few lines naming itself. the site is its website
// without the .com, and that is also where the links go.
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *items, *r, *tags, *tag;
	struct buf content = {0};
	yyjson_doc *doc;
	size_t i, n, k, m;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	items = yyjson_obj_get(yyjson_doc_get_root(doc), "items");
	yyjson_arr_foreach (items, i, n, r) {
		// the description line is "[tags] asker // is answered // score: n"
		buf_free(&content);
		buf_putc(&content, '[');
		tags = yyjson_obj_get(r, "tags");
		yyjson_arr_foreach (tags, k, m, tag)
			buf_printf(&content, "%s%s", k ? ", " : "", yyjson_get_str(tag) ? yyjson_get_str(tag) : "");
		buf_printf(&content, "] %s", json_str(yyjson_obj_get(r, "owner"), "display_name"));
		if (yyjson_is_true(yyjson_obj_get(r, "is_answered")))
			buf_puts(&content, " // is answered");
		buf_printf(&content, " // score: %s", json_text(j->a, r, "score"));

		// /q/<id> redirects to the question's real address
		add_main(j, aprintf(j->a, "https://%s/q/%s", j->e->website, json_text(j->a, r, "question_id")),
		    html_to_text(j->a, json_str(r, "title")), html_to_text(j->a, buf_str(&content)));
	}
	buf_free(&content);
	job_done(j);
}

void
stackexchange_search(struct job *j)
{
	const char *website = j->e->website;
	struct buf q = {0};
	struct fetch *f;
	char page[16];

	snprintf(page, sizeof(page), "%d", j->pageno);
	form_add(&q, "q", j->query);
	form_add(&q, "page", page);
	form_add(&q, "pagesize", "10");

	// the api calls a site by its name, which is the website without .com
	form_add(&q, "site", astrndup(j->a, website, strcspn(website, ".")));
	form_add(&q, "sort", "activity");
	form_add(&q, "order", "desc");
	f = job_request(j, aprintf(j->a, "https://api.stackexchange.com/2.3/search/advanced?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}
