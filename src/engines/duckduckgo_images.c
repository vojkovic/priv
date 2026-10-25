#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../engine.h"

static char *useragent;

static const char *
vqd_key(struct job *j)
{
	return aprintf(j->a, "duckduckgo images vqd|%s//%s", j->query, useragent);
}

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
		m = add_image(j, json_str(r, "url"), json_str(r, "title"), json_str(r, "image"));
		m->thumbnail_src = astrdup(j->a, json_str(r, "thumbnail"));
		m->source = astrdup(j->a, json_str(r, "source"));
		m->resolution = aprintf(j->a, "%s x %s", json_text(j->a, r, "width"), json_text(j->a, r, "height"));
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
	f = job_request(j, aprintf(j->a, "https://duckduckgo.com/i.js?%s", buf_str(&q)));
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
	if (f->status == 200 && (v = find_between(fetch_body(f), "vqd=\"", "\"", &n)) && n)
		cache_set(vqd_key(j), astrndup(j->a, v, n), 3600);
	else
		j->detail = aprintf(j->a, "no vqd (HTTP %ld)", f->status);
	request(j, v && n ? astrndup(j->a, v, n) : "");
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;
	const char *vqd;

	if (strlen(j->query) >= 500) {
		job_done(j);
		return;
	}
	if (!useragent)
		useragent = gen_useragent();
	if ((vqd = cache_get(vqd_key(j)))) {
		request(j, astrdup(j->a, vqd));
		return;
	}
	url_quote(&q, j->query, "", 1);
	f = job_get(j, aprintf(j->a, "https://duckduckgo.com/?q=%s&iar=images&t=h_", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "User-Agent: %s", useragent);
	job_send(j, f, vqd_response);
}

struct engine engine_duckduckgo_images = {
    .name = "duckduckgo images",
    .shortcut = "ddi",
    .website = "duckduckgo.com",
    .safesearch = 1,
    .bang = "https://duckduckgo.com/?q=%s&iax=images&ia=images",
    .category = "images",
    .paging = 1,
    .no_accept_language = 1,
    .search = search,
};
