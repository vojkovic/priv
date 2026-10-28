#include <stdlib.h>
#include <string.h>

#include "core/token.h"
#include "engines/engine.h"
#include "engines/tokens.h"

//
// Tokens that more than one engine uses
//
// an engine and its images twin (dogpile and dogpile images, google cse and
// google cse images, abc news and abc radio) ask the same site for the same
// value, so they share one copy: one request to get it, one timer to refresh
// it. each engine still decides for itself what to do with it.
//

//
// Dogpile
//
// the token is the string in window.__DP_TOKEN__="..." on the front page. it
// only lives a few minutes, so it is refreshed well before it runs out.
//

#define DOGPILE_URL "https://www.dogpile.com"

static void
dogpile_token_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	const char *tok;
	size_t n;

	// the token is the string in window.__DP_TOKEN__="..." on the front page
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
dogpile_token_fetch(struct token *t)
{
	struct fetch *f = fetch_new(DOGPILE_URL "/");

	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, dogpile_token_response, t);
}

struct token dogpile_token = {
    .name = "dogpile",
    .ttl = 240,
    .refresh = 180,
    .fetch = dogpile_token_fetch,
};

//
// Google programmable search (cse)
//
// the widget's cse.js prints a token and a few version strings, which the
// api wants back. all three are kept in one string, one per line:
//
// 1. the cse_token
// 2. the library version (cselibv)
// 3. the experiment ids, comma separated (exp)
//

static void
google_cse_token_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	const char *body, *start, *end, *tok;
	yyjson_doc *doc = NULL;
	yyjson_val *root, *exp, *v;
	struct buf b = {0};
	size_t i, n;

	if (f->code != CURLE_OK) {
		token_fail(t, "network error", SUSPEND_DEFAULT);
		goto out;
	}

	// the config is the last "({...});" call in the script, find its ends
	body = fetch_body(f);
	start = NULL;
	for (end = body; (end = strstr(end, "({")); end++)
		start = end;
	end = NULL;
	for (tok = body; (tok = strstr(tok, "});")); tok++)
		end = tok;
	if (f->status >= 400 || !start || !end || end <= start || !(doc = yyjson_read(start + 1, end - start, 0)) ||
	    !*(tok = json_str(root = yyjson_doc_get_root(doc), "cse_token"))) {
		token_fail(t, "server API error", SUSPEND_NONE);
		goto out;
	}
	buf_printf(&b, "%s\n%s\n", tok, json_str(root, "cselibVersion"));
	exp = yyjson_obj_get(root, "exp");
	yyjson_arr_foreach (exp, i, n, v)
		buf_printf(&b, "%s%s", i ? "," : "", yyjson_get_str(v) ? yyjson_get_str(v) : "");
	token_set(t, buf_str(&b));
	buf_free(&b);
out:
	yyjson_doc_free(doc);
	fetch_free(f);
}

static void
google_cse_token_fetch(struct token *t)
{
	struct fetch *f = fetch_new("https://www.google.com/cse/cse.js?cx=" GOOGLE_CSE_CX);

	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, google_cse_token_response, t);
}

struct token google_cse_token = {
    .name = "google cse",
    .ttl = 3600,
    .refresh = 3000,
    .fetch = google_cse_token_fetch,
};

//
// ABC search (algolia)
//
// the api key sits in the search page, as "apiKey":"...". abc rotates it now
// and then, until the next refresh the engines using it error out.
//

static void
abc_token_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	const char *key;
	size_t n;

	// the key is in the page's json
	if (f->code != CURLE_OK)
		token_fail(t, "network error", SUSPEND_DEFAULT);
	else if (f->status >= 400 || !(key = find_between(fetch_body(f), "\"apiKey\":\"", "\"", &n)) || !n)
		token_fail(t, "server API error", SUSPEND_NONE);
	else {
		char *v = xmalloc(n + 1);
		memcpy(v, key, n);
		v[n] = 0;
		token_set(t, v);
		free(v);
	}
	fetch_free(f);
}

static void
abc_token_fetch(struct token *t)
{
	struct fetch *f = fetch_new("https://www.abc.net.au/news/search");

	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, abc_token_response, t);
}

struct token abc_token = {
    .name = "abc",
    .ttl = 86400,
    .refresh = 80000,
    .fetch = abc_token_fetch,
};
