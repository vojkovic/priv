#include <stdlib.h>
#include <string.h>

#include "engines/engine.h"

#define API "https://api.tusksearch.com"
#define SITE "https://tusksearch.com/"

/* "x-sid-<id>: <value>", good for one request; every answer carries the next one */
static char *sid;

static void
prepare(struct fetch *f)
{
	fetch_header(f, "Referer: " SITE);
	fetch_header(f, "Sec-Fetch-Site: same-site");
	fetch_header(f, "Upgrade-Insecure-Requests:");
	fetch_header(f, "Sec-Fetch-User:");
}

static void
keep_sid(struct fetch *f)
{
	char *next = fetch_find_header(f, "x-sid-");

	if (next) {
		free(sid);
		sid = next;
	}
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *list, *r;
	struct main_result *m;
	size_t i, n;

	keep_sid(f);
	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !yyjson_is_arr(list = yyjson_ptr_get(yyjson_doc_get_root(doc), "/results/web/results"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	yyjson_arr_foreach (list, i, n, r) {
		m = add_main(j, json_str(r, "url"), html_to_text(j->a, json_str(r, "title")),
		    html_to_text(j->a, json_str(r, "description")));
		m->pubdate = (char *)format_date(j->a, json_str(r, "page_age"));
	}
	job_done(j);
}

static void
request(struct job *j, char *header)
{
	struct buf q = {0};
	struct fetch *f;

	form_add(&q, "q", j->query);
	form_add(&q, "p", aprintf(j->a, "%d", j->pageno));
	form_add(&q, "l", "center");
	f = job_request(j, aprintf(j->a, API "/Search/Web?%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "%s", header);
	free(header);
	fetch_header(f, "x-lon: 12.3456");
	fetch_header(f, "x-lat: 34.5678");
	fetch_header(f, "Origin: https://tusksearch.com");
	fetch_header(f, "Accept: application/json, text/plain, */*");
	fetch_header(f, "Sec-Fetch-Mode: cors");
	fetch_header(f, "Sec-Fetch-Dest: empty");
	fetch_header(f, "Priority: u=1, i");
	prepare(f);
	job_send(j, f, response);
}

/* embed.js is "var x=" and six arrays of char codes; the fourth and fifth are the sid header's name and value */
static char *
codes(yyjson_val *arr)
{
	size_t i, n, len = yyjson_arr_size(arr);
	char *s = xmalloc(len + 1);
	yyjson_val *c;

	yyjson_arr_foreach (arr, i, n, c)
		s[i] = (char)yyjson_get_int(c);
	s[len] = 0;
	return s;
}

static void
embed_response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *root;
	char *name, *value, *header;
	const char *body = fetch_body(f);

	if (!job_http_ok(j, f))
		return;
	if (strncmp(body, "var x=", 6) || !(doc = job_json(j, body + 6, f->body.len - 6)) ||
	    !yyjson_is_arr(root = yyjson_doc_get_root(doc)) || yyjson_arr_size(root) < 5 ||
	    !yyjson_is_arr(yyjson_arr_get(root, 3)) || !yyjson_is_arr(yyjson_arr_get(root, 4))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	name = codes(yyjson_arr_get(root, 3));
	value = codes(yyjson_arr_get(root, 4));
	if (strncmp(name, "x-sid-", 6) || strpbrk(name, "\r\n :") || strpbrk(value, "\r\n")) {
		free(name);
		free(value);
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	header = xmalloc(strlen(name) + strlen(value) + 3);
	sprintf(header, "%s: %s", name, value);
	free(name);
	free(value);
	request(j, header);
}

static void
search(struct job *j)
{
	struct fetch *f;
	char *header = sid;

	if (header) {
		sid = NULL;
		request(j, header);
		return;
	}
	f = job_request(j, API "/revcontent/embed.js");
	fetch_header(f, "Accept: */*");
	fetch_header(f, "Sec-Fetch-Mode: no-cors");
	fetch_header(f, "Sec-Fetch-Dest: script");
	fetch_header(f, "Priority:");
	prepare(f);
	job_send(j, f, embed_response);
}

struct engine engine_tusksearch = {
    .name = "tusksearch",
    .shortcut = "tusk",
    .website = "tusksearch.com",
    .bang = "https://tusksearch.com/search?q=%s",
    .category = "general",
    .paging = 1,
    .search = search,
};
