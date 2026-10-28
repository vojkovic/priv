#include <stdlib.h>
#include <string.h>

#include "engines/engine.h"

#define NEXT_PAGE_URL "https://www.youtube.com/youtubei/v1/search?key=AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8"

//
// YouTube, in two parts
//
// page one is the normal results page. the data is a json blob in the html
// (ytInitialData), so there is nothing to scrape. every page also carries a
// "continuation" token for the one after it, and later pages ask youtube's
// internal api with that token (next_response). the token travels in our own
// paging links. the key in NEXT_PAGE_URL is the one youtube's own site sends
// everyone, it is not ours.
//

/* the text of a title or a description, which is a plain string or a list of runs */
static const char *
text(struct job *j, yyjson_val *v)
{
	yyjson_val *runs = yyjson_obj_get(v, "runs"), *r;
	struct buf b = {0};
	const char *out;
	size_t i, n;

	if (!yyjson_is_arr(runs))
		return json_str(v, "simpleText");
	yyjson_arr_foreach (runs, i, n, r)
		buf_puts(&b, json_str(r, "text"));
	out = astrdup(j->a, buf_str(&b));
	buf_free(&b);
	return out;
}

/* first page has no thumbnail urls we can trust, so build them from the id */
static void
video(struct job *j, yyjson_val *v, int first_page)
{
	yyjson_val *snippet = yyjson_obj_get(v, "descriptionSnippet"), *thumbs;
	const char *id = json_str(v, "videoId"), *content;
	struct main_result *m;

	// not a video (a channel, a shelf of shorts, an ad)
	if (!*id)
		return;
	content = snippet ? text(j, snippet) : first_page ? "" : "-";
	m = add_video(j, aprintf(j->a, "https://www.youtube.com/watch?v=%s", id), text(j, yyjson_obj_get(v, "title")),
	    content);
	m->author = astrdup(j->a, text(j, yyjson_obj_get(v, "ownerText")));
	m->length = astrdup(j->a, text(j, yyjson_obj_get(v, "lengthText")));
	if (first_page) {
		m->thumbnail = aprintf(j->a, "https://i.ytimg.com/vi/%s/hqdefault.jpg", id);
	} else {
		thumbs = yyjson_obj_get(yyjson_obj_get(v, "thumbnail"), "thumbnails");
		m->thumbnail = astrdup(j->a, json_str(yyjson_arr_get_last(thumbs), "url"));
	}
}

static void
items(struct job *j, yyjson_val *contents, int first_page)
{
	yyjson_val *item;
	size_t i, n;

	yyjson_arr_foreach (contents, i, n, item)
		video(j, yyjson_obj_get(item, "videoRenderer"), first_page);
}

static void
next_token(struct job *j, yyjson_val *item)
{
	const char *tok = yyjson_get_str(yyjson_ptr_get(item,
	    "/continuationItemRenderer/continuationEndpoint/"
	    "continuationCommand/token"));

	if (tok && *tok)
		j->next_token = astrdup(j->a, tok);
}

static void
first_response(struct job *j, struct fetch *f)
{
	const char *data;
	yyjson_val *sections, *section;
	yyjson_doc *doc;
	size_t i, n, len = 0;

	if (!job_http_ok(j, f))
		return;

	//
	// Get the page data
	//
	if (!(data = find_between(fetch_body(f), "ytInitialData = ", ";</script>", &len)) || !len) {
		job_done(j);
		return;
	}
	if (!(doc = job_json(j, data, len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	sections = yyjson_ptr_get(yyjson_doc_get_root(doc),
	    "/contents/twoColumnSearchResultsRenderer/primaryContents/sectionListRenderer/contents");

	//
	// Get results
	//
	// the sections are the video list and, last, the next page's token
	//
	yyjson_arr_foreach (sections, i, n, section) {
		next_token(j, section);
		items(j, yyjson_ptr_get(section, "/itemSectionRenderer/contents"), 1);
	}
	job_done(j);
}

static void
next_response(struct job *j, struct fetch *f)
{
	yyjson_val *list;
	yyjson_doc *doc;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	list = yyjson_ptr_get(yyjson_doc_get_root(doc),
	    "/onResponseReceivedCommands/0/appendContinuationItemsAction/continuationItems");
	items(j, yyjson_ptr_get(yyjson_arr_get(list, 0), "/itemSectionRenderer/contents"), 0);
	next_token(j, yyjson_arr_get(list, 1));
	job_done(j);
}

static void
search(struct job *j)
{
	const char *token = j->pageno > 1 ? j->token : NULL, *t;
	yyjson_mut_doc *d;
	yyjson_mut_val *root, *client;
	struct buf q = {0};
	struct fetch *f;
	char *body;

	//
	// Page two and up: ask the api with the continuation token
	//
	if (token) {
		d = yyjson_mut_doc_new(NULL);
		root = yyjson_mut_obj(d);
		client = yyjson_mut_obj(d);
		yyjson_mut_doc_set_root(d, root);
		yyjson_mut_obj_add_str(d, client, "clientName", "WEB");
		yyjson_mut_obj_add_str(d, client, "clientVersion", "2.20210310.12.01");
		yyjson_mut_obj_add_val(d, yyjson_mut_obj_add_obj(d, root, "context"), "client", client);
		yyjson_mut_obj_add_strcpy(d, root, "continuation", token);
		body = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
		yyjson_mut_doc_free(d);
		f = job_request(j, NEXT_PAGE_URL);
		fetch_post(f, body, "application/json");
		free(body);
		fetch_cookie(f, "CONSENT", "YES+");
		f->http_version = CURL_HTTP_VERSION_3;
		job_send(j, f, next_response);
		return;
	}

	//
	// Page one: the results page
	//
	url_quote(&q, j->query, "", 1);
	buf_printf(&q, "&page=%d", j->pageno);

	//
	// The upload date filter is a protobuf, in the sp parameter
	//
	// 12 02 08 <n>, base64 encoded, with the = padding url encoded twice
	//
	// day     n=2   EgIIAg%253D%253D
	// week    n=3   EgIIAw%253D%253D
	// month   n=4   EgIIBA%253D%253D
	// year    n=5   EgIIBQ%253D%253D
	//
	// (we send the middle two characters as "Ag", "Aw", "BA", "BQ")
	//
	if ((t = time_range_code(j->filters, "Ag", "Aw", "BA", "BQ")))
		buf_printf(&q, "&sp=EgII%s%%253D%%253D", t);
	f = job_request(j, aprintf(j->a, "https://www.youtube.com/results?search_query=%s", buf_str(&q)));
	buf_free(&q);

	// agree to the cookie banner up front or youtube redirects to it
	fetch_cookie(f, "CONSENT", "YES+");
	f->http_version = CURL_HTTP_VERSION_3;
	job_send(j, f, first_response);
}

struct engine engine_youtube = {
    .name = "youtube",
    .shortcut = "yt",
    .website = "www.youtube.com",
    .bang = "https://www.youtube.com/results?search_query=%s",
    .category = "videos",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
