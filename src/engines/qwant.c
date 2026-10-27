#include <stdlib.h>
#include <string.h>

#include "../engine.h"

#define API_URL "https://api.qwant.com/v3/search/web"
#define SEASON_MS 12000
#define RETRY_SECS 15

static char *datadome;
static double datadome_since;
static int warming;

/* the datadome cookie changes on every response, but only the age of the first one matters */
static void
remember(struct fetch *f)
{
	const char *v;
	size_t n;

	if (!(v = find_between(buf_str(&f->response), "datadome=", ";", &n)) || !n)
		return;
	if (!datadome)
		datadome_since = now_ms();
	free(datadome);
	datadome = xmalloc(n + 1);
	memcpy(datadome, v, n);
	datadome[n] = 0;
}

static void
warm_response(struct fetch *f, void *arg)
{
	(void)arg;
	warming = 0;
	remember(f);
	fetch_free(f);
}

static void
warm(void)
{
	struct fetch *f;

	if (warming)
		return;
	warming = 1;
	f = fetch_new(API_URL
	    "?q=test&count=10&locale=en_US&offset=0&device=desktop&tgp=2&safesearch=1&displayed=true&llm=false");
	fetch_header(f, "Accept: application/json, text/plain, */*");
	fetch_timeout(f, 10000);
	fetch_start(f, warm_response, NULL);
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *rows, *row, *items, *r;
	yyjson_doc *doc = NULL;
	size_t i, n, k, m;

	if (!job_ok(j, f))
		return;
	remember(f);
	if (f->status == 403 && strstr(buf_str(&f->response), "datadome=") && !strstr(fetch_body(f), "\"status\"")) {
		job_fail(j, "CAPTCHA", RETRY_SECS);
		return;
	}
	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    strcmp(json_str(yyjson_doc_get_root(doc), "status"), "success") ||
	    !yyjson_is_arr(rows = yyjson_ptr_get(yyjson_doc_get_root(doc), "/data/result/items/mainline"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (rows, i, n, row) {
		if (strcmp(json_str(row, "type"), "web"))
			continue;
		items = yyjson_obj_get(row, "items");
		yyjson_arr_foreach (items, k, m, r)
			add_main(j, json_str(r, "url"), html_to_text(j->a, json_str(r, "title")),
			    html_to_text(j->a, json_str(r, "desc")));
	}
	job_done(j);
}

static void
search(struct job *j)
{
	const char *locale = j->filters->locale->ddg_lang;
	struct buf q = {0};
	struct fetch *f;

	if (!datadome) {
		warm();
		job_fail(j, "warming up", RETRY_SECS);
		return;
	}
	if (now_ms() - datadome_since < SEASON_MS) {
		job_fail(j, "warming up", RETRY_SECS);
		return;
	}
	form_add(&q, "q", j->query);
	form_add(&q, "count", "10");
	form_add(&q, "locale", strchr(locale, '_') ? locale : "en_US");
	form_add(&q, "offset", "0");
	form_add(&q, "device", "desktop");
	form_add(&q, "tgp", "2");
	form_add(&q, "safesearch", aprintf(j->a, "%d", j->filters->safesearch));
	form_add(&q, "displayed", "true");
	form_add(&q, "llm", "false");
	f = job_request(j, aprintf(j->a, API_URL "?%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "Accept: application/json, text/plain, */*");
	fetch_cookie(f, "datadome", datadome);
	job_send(j, f, response);
}

struct engine engine_qwant = {
    .name = "qwant",
    .shortcut = "qw",
    .website = "www.qwant.com",
    .safesearch = 1,
    .bang = "https://www.qwant.com/?q=%s",
    .category = "general",
    .init = warm,
    .search = search,
};
