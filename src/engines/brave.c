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
video(struct job *j, yyjson_mut_val *r)
{
	yyjson_mut_val *v = get(r, "video"), *views = get(v, "views");
	struct main_result *m = add_video(j, str(r, "url"), str(r, "title"), str(r, "description"));

	m->thumbnail = astrdup(j->a, str(get(r, "thumbnail"), "src"));
	m->pubdate = astrdup(j->a, str(r, "age"));
	m->length = astrdup(j->a, str(v, "duration"));
	m->author = astrdup(j->a, str(v, "creator"));
	if (yyjson_mut_is_int(views))
		m->views = format_count(j->a, yyjson_mut_get_sint(views));
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *data, *body, *results, *r, *thumb;
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
	body = get(get(data, "body"), "response");
	results = get(get(body, "web"), "results");
	if (!yyjson_mut_is_arr(results)) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		goto out;
	}
	yyjson_mut_arr_foreach (results, i, n, r) {
		m = add_main(j, str(r, "url"), str(r, "title"), html_to_text(j->a, str(r, "description")));
		m->pubdate = astrdup(j->a, str(r, "age"));
		thumb = get(r, "thumbnail");
		if (yyjson_mut_is_obj(thumb) && !yyjson_mut_is_true(get(thumb, "logo")))
			m->thumbnail = astrdup(j->a, str(thumb, "src"));
	}
	results = get(get(body, "videos"), "results");
	yyjson_mut_arr_foreach (results, i, n, r)
		video(j, r);
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
	const char *t;
	char page[16];

	form_add(&q, "q", j->query);
	form_add(&q, "source", "web");
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", j->pageno - 1);
		form_add(&q, "offset", page);
	}
	if ((t = time_range_code(j->filters, "pd", "pw", "pm", "py")))
		form_add(&q, "tf", t);
	f = job_request(j, aprintf(j->a, "https://search.brave.com/search?%s", buf_str(&q)));
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

/* whether this engine narrows results to the locale's language, for the preferences page */
static int
language(const struct locale *l)
{
	return strcmp(l->brave_country, "all") != 0;
}

struct engine engine_brave = {
    .name = "brave",
    .shortcut = "br",
    .website = "search.brave.com",
    .language = language,
    .safesearch = 1,
    .bang = "https://search.brave.com/search?q=%s",
    .categories = {"general"},
    .paging = 1,
    .max_page = 10,
    .time_range = 1,
    .search = search,
};
