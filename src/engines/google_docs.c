#include <stdlib.h>
#include <string.h>

#include "../engine.h"

#define PAGE_SIZE 20

/* a public document that still allows the explore panel */
#define DOCUMENT "https://docs.google.com/document/d/1X8Gd2RPfsQdiYVcrUmgk_RpQ7iAl-UQd9vlvpZ8Itvs"

static const char *
at(yyjson_val *arr, size_t i)
{
	const char *s = yyjson_get_str(yyjson_arr_get(arr, i));

	return s ? s : "";
}

/* the knowledge card: [id, [title, type], [text, [source url, source]], [image, w, h, page], [[label, [values]]]] */
static void
card(struct job *j, yyjson_val *c)
{
	yyjson_val *name = yyjson_arr_get(c, 1), *about = yyjson_arr_get(c, 2), *source = yyjson_arr_get(about, 1),
	           *fact;
	struct infobox *ib;
	struct buf values = {0};
	size_t i, n, k, m;
	yyjson_val *v;

	if (!*at(name, 0))
		return;
	ib = add_infobox(j, at(name, 0));
	ib->description = astrdup(j->a, at(name, 1));
	ib->content = astrdup(j->a, at(about, 0));
	ib->img_src = astrdup(j->a, at(yyjson_arr_get(c, 3), 0));
	/* the source's url as the id merges this into wikipedia's own infobox when that is the source */
	if (*at(source, 0)) {
		ib->id = astrdup(j->a, at(source, 0));
		infobox_url(j, ib, *at(source, 1) ? at(source, 1) : ib->id, ib->id, NULL, 0);
	}
	yyjson_arr_foreach (yyjson_arr_get(c, 4), i, n, fact) {
		yyjson_arr_foreach (yyjson_arr_get(fact, 1), k, m, v)
			buf_printf(&values, "%s%s", values.len ? ", " : "", yyjson_get_str(v) ? yyjson_get_str(v) : "");
		if (*at(fact, 0) && values.len)
			infobox_attr(j, ib, at(fact, 0), buf_str(&values), NULL);
		values.len = 0;
	}
	buf_free(&values);
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *results, *item, *r;
	const char *body = fetch_body(f), *json = strchr(body, '\n');
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!json || !(doc = job_json(j, json, strlen(json))) ||
	    !yyjson_is_arr(results = yyjson_arr_get(yyjson_arr_get(yyjson_doc_get_root(doc), 0), 1))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	/* organic results are [[url, title, snippet, ...]], the knowledge card is [null, card] */
	yyjson_arr_foreach (results, i, n, item) {
		r = yyjson_arr_get(item, 0);
		if (!yyjson_is_arr(r)) {
			if (j->pageno == 1)
				card(j, yyjson_arr_get(item, 1));
			continue;
		}
		add_main(j, yyjson_get_str(yyjson_arr_get(r, 0)), yyjson_get_str(yyjson_arr_get(r, 1)),
		    yyjson_get_str(yyjson_arr_get(r, 2)));
	}
	job_done(j);
}

/* the offset travels as a protobuf with it in field 1, base64 encoded: 0 is CAA= and 20 is CBQ= */
static const char *
offset_token(struct job *j, unsigned offset)
{
	unsigned char p[6] = {0x08};
	size_t n = 1;

	do {
		p[n++] = (unsigned char)((offset & 0x7f) | (offset > 0x7f ? 0x80 : 0));
		offset >>= 7;
	} while (offset && n < sizeof(p));
	return base64(j->a, p, n);
}

static void
search(struct job *j)
{
	const struct locale *l = j->filters->locale;
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_arr(d), *page, *lang;
	struct buf q = {0};
	struct fetch *f;
	char *json;

	yyjson_mut_doc_set_root(d, root);
	yyjson_mut_arr_add_str(d, root, "documentsuggest.search.search_request");
	yyjson_mut_arr_add_str(d, root, j->query);
	page = yyjson_mut_arr_add_arr(d, root);
	yyjson_mut_arr_add_int(d, page, PAGE_SIZE);
	yyjson_mut_arr_add_str(d, page, offset_token(j, (unsigned)(j->pageno - 1) * PAGE_SIZE));
	/* explore has no real region or language setting: any language here gives american results, none gives
	 * british ones, so only american and canadian english ask for it */
	if (!strcmp(l->tag, "en-US") || !strcmp(l->tag, "en-CA")) {
		lang = yyjson_mut_arr_add_arr(d, root);
		yyjson_mut_arr_add_str(d, lang, "en");
		yyjson_mut_arr_add_str(d, lang, "lang_en");
	} else {
		yyjson_mut_arr_add_null(d, root);
	}
	yyjson_mut_arr_add_int(d, root, 1);
	json = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
	yyjson_mut_doc_free(d);

	form_add(&q, "request", json);
	free(json);
	f = job_request(j, DOCUMENT "/explore/search");
	fetch_post(f, buf_str(&q), "application/x-www-form-urlencoded;charset=UTF-8");
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_google_docs = {
    .name = "google docs",
    .shortcut = "god",
    .website = "docs.google.com",
    .bang = "https://www.google.com/search?q=%s",
    .categories = {"general"},
    .paging = 1,
    .search = search,
};
