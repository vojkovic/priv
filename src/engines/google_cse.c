#include <string.h>
#include <time.h>

#include "../engine.h"
#include "../token.h"

#define CX "partner-pub-8993703457585266:4862972284"
#define PAGE_SIZE 20

static int
range_days(const char *r)
{
	return !strcmp(r, "day") ? 1 : !strcmp(r, "week") ? 7 : !strcmp(r, "month") ? 30 : 365;
}

static void
web_item(struct job *j, yyjson_val *item)
{
	const char *url = json_str(item, "unescapedUrl");
	struct main_result *m;

	if (!*url)
		return;
	m = add_main(j, url, json_str(item, "titleNoFormatting"), json_str(item, "contentNoFormatting"));
	m->thumbnail =
	    astrdup(j->a, json_str(yyjson_obj_get(yyjson_obj_get(item, "richSnippet"), "cseThumbnail"), "src"));
}

static void
response(struct job *j, struct fetch *f)
{
	const char *body, *start, *end;
	yyjson_doc *doc;
	yyjson_val *root, *err, *item, *results;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	body = fetch_body(f);
	start = strchr(body, '{');
	end = strrchr(body, '}');
	if (!start || !end || end < start || !(doc = job_json(j, start, end - start + 1))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	root = yyjson_doc_get_root(doc);
	if ((err = yyjson_obj_get(root, "error"))) {
		const char *msg = yyjson_get_str(yyjson_obj_get(err, "message"));
		fprintf(stderr, "google cse: %s\n", msg ? msg : "unknown error");
		if (yyjson_get_int(yyjson_obj_get(err, "code")) == 429)
			job_fail(j, "too many requests", suspend_too_many_requests);
		else
			job_fail(j, "server API error", SUSPEND_NONE);
		return;
	}
	results = yyjson_obj_get(root, "results");
	yyjson_arr_foreach (results, i, n, item)
		web_item(j, item);
	job_done(j);
}

static void
request(struct job *j, const char *token)
{
	static const char *safe[] = {"off", "medium", "high"};
	const struct locale *l = j->filters->locale;
	struct buf q = {0};
	struct fetch *f;
	const char *tok, *libv, *exp;
	char page[16], from[16], to[16];
	time_t now = time(NULL), then;

	tok = token;
	libv = strchr(tok, '\n') + 1;
	exp = strchr(libv, '\n') + 1;
	form_add(&q, "rsz", "filtered_cse");
	form_add(&q, "num", "20");
	form_add(&q, "hl", l->google_hl);
	form_add(&q, "cselibv", astrndup(j->a, libv, exp - libv - 1));
	form_add(&q, "cx", CX);
	form_add(&q, "q", j->query);
	form_add(&q, "safe", safe[j->filters->safesearch]);
	form_add(&q, "cse_tok", astrndup(j->a, tok, libv - tok - 1));
	form_add(&q, "callback", "_");
	form_add(&q, "rurl", "");
	form_add(&q, "searchtype", "");
	if (j->filters->time_range) {
		then = now - range_days(j->filters->time_range) * 86400;
		strftime(from, sizeof(from), "%Y%m%d", localtime(&then));
		strftime(to, sizeof(to), "%Y%m%d", localtime(&now));
		form_add(&q, "sort", aprintf(j->a, "date:r:%s:%s", from, to));
	}
	if (*l->google_gl)
		form_add(&q, "gl", l->google_gl);
	if (*exp)
		form_add(&q, "exp", exp);
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", (j->pageno - 1) * PAGE_SIZE);
		form_add(&q, "start", page);
	}
	f = job_request(j, aprintf(j->a, "https://cse.google.com/cse/element/v1?%s", buf_str(&q)));
	buf_free(&q);
	fetch_cookie(f, "CONSENT", "YES+");
	fetch_header(f, "Accept: */*");
	fetch_header(f, "Referer: https://cse.google.com/");
	f->http_version = CURL_HTTP_VERSION_3;
	job_send(j, f, response);
}

static void
token_response(struct fetch *f, void *arg)
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
token_fetch(struct token *t)
{
	struct fetch *f = fetch_new("https://www.google.com/cse/cse.js?cx=" CX);

	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, token_response, t);
}

static struct token token = {
    .name = "google cse",
    .ttl = 3600,
    .refresh = 3000,
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

struct engine engine_google_cse = {
    .name = "google cse",
    .shortcut = "goc",
    .website = "www.google.com",
    .safesearch = 1,
    .bang = "https://www.google.com/search?q=%s",
    .categories = {"general"},
    .paging = 1,
    .max_page = 5,
    .time_range = 1,
    .init = init,
    .search = search,
};
