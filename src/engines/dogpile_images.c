#include <stdlib.h>
#include <string.h>

#include "../engine.h"
#include "../token.h"

#define BASE_URL "https://www.dogpile.com"

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *results, *r;
	struct main_result *m;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(results = yyjson_obj_get(yyjson_doc_get_root(doc), "results"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (results, i, n, r) {
		m = add_image(j, json_str(r, "altClickUrl"), html_to_text(j->a, json_str(r, "title")),
		    json_str(r, "clickUrl"));
		m->content = html_to_text(j->a, json_str(r, "description"));
		m->thumbnail_src = astrdup(j->a, json_str(r, "thumbnailUrl"));
		m->resolution = aprintf(j->a, "%sx%s", json_text(j->a, r, "width"), json_text(j->a, r, "height"));
		m->img_format = astrdup(j->a, json_str(r, "format"));
	}
	job_done(j);
}

static void
request(struct job *j, const char *token)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_obj(d);
	struct fetch *f;
	char *body;

	yyjson_mut_doc_set_root(d, root);
	yyjson_mut_obj_add_str(d, root, "q", j->query);
	yyjson_mut_obj_add_str(d, root, "qadf", (const char *[]){"none", "moderate", "heavy"}[j->filters->safesearch]);
	yyjson_mut_obj_add_int(d, root, "page", j->pageno);
	body = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
	yyjson_mut_doc_free(d);

	f = job_request(j, BASE_URL "/api/images");
	fetch_header(f, "Origin: " BASE_URL);
	fetch_header(f, "Sec-Fetch-Site: same-origin");
	fetch_header(f, "Sec-Fetch-Mode: cors");
	fetch_header(f, "x-dogpile-token: %s", token);
	fetch_cookie(f, "dp_api_token", "1");
	fetch_post(f, body, "application/json");
	free(body);
	job_send(j, f, response);
}

static void
token_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	const char *tok;
	size_t n;

	if (f->code != CURLE_OK)
		token_fail(t, "network error", SUSPEND_DEFAULT);
	else if (f->status >= 400 || !(tok = find_between(fetch_body(f), "window.__DP_TOKEN__=\"", "\"", &n)) || !n)
		token_fail(t, "server API error", SUSPEND_NONE);
	else {
		char *v = xmalloc(n + 1);
		memcpy(v, tok, n);
		v[n] = 0;
		token_set(t, v);
		free(v);
	}
	fetch_free(f);
}

static void
token_fetch(struct token *t)
{
	struct fetch *f = fetch_new(BASE_URL "/");

	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, token_response, t);
}

static struct token token = {
    .name = "dogpile images",
    .ttl = 240,
    .refresh = 180,
    .fetch = token_fetch,
};

static void
init(void)
{
	token_warm(&token);
}

static void
search(struct job *j)
{
	token_get(j, &token, request);
}

struct engine engine_dogpile_images = {
    .name = "dogpile images",
    .shortcut = "dogi",
    .website = "www.dogpile.com",
    .safesearch = 1,
    .bang = "https://www.dogpile.com/serp/images?q=%s",
    .categories = {"images"},
    .paging = 1,
    .init = init,
    .search = search,
};
