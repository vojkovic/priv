#include <string.h>

#include "../engine.h"
#include "../js.h"

static yyjson_mut_val *
get(yyjson_mut_val *v, const char *key)
{
	return yyjson_mut_obj_get(v, key);
}

static const char *
str(yyjson_mut_val *v, const char *key)
{
	const char *s = yyjson_mut_get_str(get(v, key));

	return s ? s : "";
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *data, *results, *r;
	struct main_result *m;
	size_t i, n;

	if (!job_http_ok(j, f))
		goto out;
	if (!(data = js_page_data(d, fetch_body(f)))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		goto out;
	}
	data = get(yyjson_mut_arr_get(data, 1), "data");
	if (yyjson_mut_is_true(get(data, "noResults"))) {
		job_done(j);
		goto out;
	}
	results = get(get(get(data, "response"), "news"), "results");
	if (!yyjson_mut_is_arr(results)) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		goto out;
	}
	yyjson_mut_arr_foreach (results, i, n, r) {
		m = add_main(j, str(r, "url"), str(r, "title"), html_to_text(j->a, str(r, "description")));
		m->thumbnail = astrdup(j->a, str(get(r, "thumbnail"), "src"));
		m->pubdate = astrdup(j->a, str(r, "age"));
	}
	job_done(j);
out:
	yyjson_mut_doc_free(d);
}

static void
search(struct job *j)
{
	static const char *safe[] = {"off", "moderate", "strict"};
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "source", "web");
	f = job_request(j, aprintf(j->a, "https://search.brave.com/news?%s", buf_str(&q)));
	buf_free(&q);
	curl_easy_setopt(f->h, CURLOPT_FORBID_REUSE, 1L);
	f->http_version = CURL_HTTP_VERSION_3ONLY;
	fetch_cookie(f, "safesearch", safe[j->filters->safesearch]);
	fetch_cookie(f, "useLocation", "0");
	fetch_cookie(f, "summarizer", "0");
	fetch_cookie(f, "country", j->filters->locale->brave_country);
	fetch_cookie(f, "ui_lang", j->filters->locale->brave_ui_lang);
	job_send(j, f, response);
}

struct engine engine_brave_news = {
    .name = "brave.news",
    .shortcut = "brnews",
    .website = "search.brave.com",
    .safesearch = 1,
    .bang = "https://search.brave.com/news?q=%s",
    .categories = {"news"},
    .search = search,
};
