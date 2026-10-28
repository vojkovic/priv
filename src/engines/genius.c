#include <string.h>
#include <time.h>

#include "engines/engine.h"

#define PAGE_SIZE 5

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *sections, *section, *hits, *hit, *r;
	struct main_result *m;
	const char *type;
	size_t i, n, k, c;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(
	        sections = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "response"), "sections"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	/* songs and albums only: lyric hits repeat the songs and artists are just names */
	yyjson_arr_foreach (sections, i, n, section) {
		type = json_str(section, "type");
		if (strcmp(type, "song") && strcmp(type, "album"))
			continue;
		hits = yyjson_obj_get(section, "hits");
		yyjson_arr_foreach (hits, k, c, hit) {
			r = yyjson_obj_get(hit, "result");
			m = add_main(j, json_str(r, "url"), json_str(r, "full_title"),
			    *type == 's' ? json_str(r, "artist_names") : json_str(r, "name_with_artist"));
			if (!m)
				continue;
			m->thumbnail = astrdup(j->a,
			    *type == 's' ? json_str(r, "song_art_image_thumbnail_url") :
			                   json_str(r, "cover_art_thumbnail_url"));
			m->pubdate = astrdup(j->a, json_str(r, "release_date_for_display"));
		}
	}
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "per_page", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "page", aprintf(j->a, "%d", j->pageno));
	f = job_request(j, aprintf(j->a, "https://genius.com/api/search/multi?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_genius = {
    .name = "genius",
    .shortcut = "gen",
    .website = "genius.com",
    .bang = "https://genius.com/search?q=%s",
    .category = "music",
    .paging = 1,
    .search = search,
};
