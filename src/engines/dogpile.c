#include <stdlib.h>
#include <string.h>

#include "engines/engine.h"
#include "engines/tokens.h"

#define BASE_URL "https://www.dogpile.com"

//
// Dogpile's json api
//
// the api wants a token that the front page prints into its own javascript
// (window.__DP_TOKEN__). it only lives a few minutes, so tokens.c fetches a
// fresh one before the old one runs out and searches never wait for it. the
// images engine uses the same token.
//

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *results, *r;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(results = yyjson_obj_get(yyjson_doc_get_root(doc), "results"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (results, i, n, r)
		add_main(j, json_str(r, "clickUrl"), html_to_text(j->a, json_str(r, "title")),
		    html_to_text(j->a, json_str(r, "description")));
	job_done(j);
}

static void
request(struct job *j, const char *token)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_obj(d);
	struct fetch *f;
	char *body;

	// the same json the site posts
	yyjson_mut_doc_set_root(d, root);
	yyjson_mut_obj_add_str(d, root, "q", j->query);
	yyjson_mut_obj_add_str(d, root, "qadf", (const char *[]){"none", "moderate", "heavy"}[j->filters->safesearch]);
	yyjson_mut_obj_add_int(d, root, "page", j->pageno);
	body = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
	yyjson_mut_doc_free(d);

	// it checks that we look like the site's own script
	f = job_request(j, BASE_URL "/api/search");
	fetch_header(f, "Origin: " BASE_URL);
	fetch_header(f, "Referer: " BASE_URL "/search");
	fetch_header(f, "Sec-Fetch-Site: same-origin");
	fetch_header(f, "Sec-Fetch-Mode: cors");
	fetch_header(f, "x-dogpile-token: %s", token);
	fetch_cookie(f, "dp_api_token", "1");
	fetch_post(f, body, "application/json");
	free(body);
	job_send(j, f, response);
}

static void
init(void)
{
	token_warm(&dogpile_token);
}

static void
search(struct job *j)
{
	token_get(j, &dogpile_token, request);
}

struct engine engine_dogpile = {
    .name = "dogpile",
    .shortcut = "dog",
    .website = "www.dogpile.com",
    .safesearch = 1,
    .bang = "https://www.dogpile.com/serp?q=%s",
    .category = "general",
    .paging = 1,
    .init = init,
    .search = search,
};
