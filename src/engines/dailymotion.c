#include <time.h>

#include "engines/engine.h"

#define PAGE_SIZE 10

//
// Dailymotion's public api, plain json. no tokens or tricks
//

static const char *
iso_date(struct arena *a, long long secs)
{
	char out[32];
	time_t t = (time_t)secs;
	struct tm tm;

	if (secs <= 0 || !gmtime_r(&t, &tm))
		return NULL;
	strftime(out, sizeof(out), "%Y-%m-%d", &tm);
	return format_date(a, out);
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *list, *r;
	struct main_result *m;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(list = yyjson_obj_get(yyjson_doc_get_root(doc), "list"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (list, i, n, r) {
		m = add_video(j, json_str(r, "url"), json_str(r, "title"),
		    html_to_text(j->a, json_str(r, "description")));
		if (!m)
			continue; // no usable link
		m->thumbnail = astrdup(j->a, json_str(r, "thumbnail_360_url"));
		m->length = format_duration(j->a, (long)yyjson_get_sint(yyjson_obj_get(r, "duration")));
		m->pubdate = (char *)iso_date(j->a, yyjson_get_sint(yyjson_obj_get(r, "created_time")));
	}
	job_done(j);
}

static void
search(struct job *j)
{
	static const long ranges[] = {86400, 7 * 86400, 31 * 86400, 365 * 86400};
	const char *range = time_range_code(j->filters, "0", "1", "2", "3");
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "search", j->query);
	form_add(&q, "family_filter", j->filters->safesearch ? "true" : "false");
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno));
	form_add(&q, "limit", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "sort", "relevance");
	form_add(&q, "password_protected", "false");
	form_add(&q, "private", "false");
	form_add(&q, "fields", "title,url,description,duration,created_time,thumbnail_360_url");
	// the time filter is "created after" a unix time
	if (range)
		form_add(&q, "created_after", aprintf(j->a, "%lld", (long long)time(NULL) - ranges[*range - '0']));
	f = job_request(j, aprintf(j->a, "https://api.dailymotion.com/videos?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_dailymotion = {
    .name = "dailymotion",
    .shortcut = "dm",
    .website = "www.dailymotion.com",
    .safesearch = 1,
    .bang = "https://www.dailymotion.com/search/%s",
    .category = "videos",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
