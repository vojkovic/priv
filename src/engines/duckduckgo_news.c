#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engines/engine.h"

static char *useragent;

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *results, *r;
	struct main_result *m;
	size_t i, n;
	time_t date;
	struct tm tm;
	char day[16];

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(results = yyjson_obj_get(yyjson_doc_get_root(doc), "results"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (results, i, n, r) {
		m = add_main(j, json_str(r, "url"), json_str(r, "title"), html_to_text(j->a, json_str(r, "excerpt")));
		m->thumbnail = astrdup(j->a, json_str(r, "image"));
		if ((date = (time_t)yyjson_get_sint(yyjson_obj_get(r, "date"))) > 0 && gmtime_r(&date, &tm)) {
			strftime(day, sizeof(day), "%Y-%m-%d", &tm);
			m->pubdate = (char *)format_date(j->a, day);
		}
	}
	job_done(j);
}

static const char *
country(struct job *j, const char *tag)
{
	char *ct;
	size_t i;

	if (!strcmp(tag, "all"))
		return "EN";
	ct = astrndup(j->a, tag, strcspn(tag, "-"));
	for (i = 0; ct[i]; i++)
		ct[i] = (char)toupper((unsigned char)ct[i]);
	return ct;
}

static void
request(struct job *j, const char *vqd)
{
	static const char *safe[] = {"-2", NULL, "1"};
	const struct locale *l = j->filters->locale;
	const char *p = safe[j->filters->safesearch];
	struct buf q = {0};
	struct fetch *f;
	char page[16];

	if (*vqd)
		j->next_token = vqd;
	form_add(&q, "o", "json");
	form_add(&q, "q", j->query);
	form_add(&q, "u", "bing");
	form_add(&q, "l", l->ddg_region);
	form_add(&q, "bpia", "1");
	form_add(&q, "vqd", vqd);
	form_add(&q, "a", "h_");
	form_add(&q, "ct", country(j, l->tag));
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", (j->pageno - 1) * 100);
		form_add(&q, "s", page);
	}
	if (p)
		form_add(&q, "p", p);
	f = job_request(j, aprintf(j->a, "https://duckduckgo.com/news.js?%s", buf_str(&q)));
	buf_free(&q);
	fetch_impersonate(f, NULL, 0);
	fetch_header(f, "User-Agent: %s", useragent);
	fetch_header(f, "Accept: */*");
	fetch_header(f, "Referer: https://duckduckgo.com/");
	fetch_header(f, "Host: duckduckgo.com");
	fetch_cookie(f, "ad", l->ddg_lang);
	fetch_cookie(f, "ah", l->ddg_region);
	fetch_cookie(f, "l", l->ddg_region);
	if (p)
		fetch_cookie(f, "p", p);
	job_send(j, f, response);
}

static void
vqd_response(struct job *j, struct fetch *f)
{
	const char *v = NULL;
	size_t n = 0;

	if (!job_ok(j, f))
		return;
	if (!(f->status == 200 && (v = find_between(fetch_body(f), "vqd=\"", "\"", &n)) && n))
		j->detail = aprintf(j->a, "no vqd (HTTP %ld)", f->status);
	request(j, v && n ? astrndup(j->a, v, n) : "");
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	if (strlen(j->query) >= 500) {
		job_done(j);
		return;
	}
	if (!useragent)
		useragent = gen_useragent();
	if (j->token) {
		request(j, j->token);
		return;
	}
	url_quote(&q, j->query, "", 1);
	f = job_get(j, aprintf(j->a, "https://duckduckgo.com/?q=%s&iar=images&t=h_", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "User-Agent: %s", useragent);
	job_send(j, f, vqd_response);
}

struct engine engine_duckduckgo_news = {
    .name = "duckduckgo news",
    .shortcut = "ddn",
    .website = "duckduckgo.com",
    .safesearch = 1,
    .bang = "https://duckduckgo.com/?q=%s&iar=news&ia=news",
    .category = "news",
    .paging = 1,
    .no_accept_language = 1,
    .search = search,
};
