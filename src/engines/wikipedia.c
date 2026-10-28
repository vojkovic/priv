#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engines/engine.h"

#define HITS 10

//
// Wikipedia, as an infobox and no results
//
// the query is tried as an article title (search() -> summary()). when that
// lands on a disambiguation page we ask the search api for the pages that
// share the name instead (meanings()), pick the most viewed as the box and
// list the others as "also refers to".
//
#define MAX_ALSO 4

static double
views(yyjson_val *page)
{
	yyjson_val *days = yyjson_obj_get(page, "pageviews"), *k, *v;
	double sum = 0;
	size_t i, n;

	yyjson_obj_foreach (days, i, n, k, v)
		sum += yyjson_get_num(v);
	return sum;
}

static int
by_views(const void *x, const void *y)
{
	double a = views(*(yyjson_val *const *)x), b = views(*(yyjson_val *const *)y);

	return (a < b) - (a > b);
}

static int
same_base(const char *title, const char *base)
{
	size_t n = strlen(base);

	return !strncasecmp(title, base, n) && (!title[n] || !strncmp(title + n, " (", 2));
}

static const char *
first_paragraph(struct job *j, const char *s)
{
	return astrndup(j->a, s, strcspn(s, "\n"));
}

static void
meanings(struct job *j, struct fetch *f)
{
	yyjson_val *pages, *p, *hits[HITS], *main = NULL;
	const char *title = j->query;
	struct infobox *ib;
	yyjson_doc *doc;
	size_t i, n = 0, idx, max, also = 0;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}

	//
	// Pick the main page
	//
	// no disambiguation pages, then the most read page whose title matches
	//
	pages = yyjson_ptr_get(yyjson_doc_get_root(doc), "/query/pages");
	yyjson_arr_foreach (pages, idx, max, p)
		if (n < HITS && !yyjson_obj_get(yyjson_obj_get(p, "pageprops"), "disambiguation"))
			hits[n++] = p;
	qsort(hits, n, sizeof(*hits), by_views);
	for (i = 0; i < n && !main; i++)
		if (*json_str(hits[i], "extract") && same_base(json_str(hits[i], "title"), title))
			main = hits[i];
	if (!main) {
		// nothing that is clearly the one
		job_done(j);
		return;
	}

	//
	// Build the infobox
	//
	ib = add_infobox(j, json_str(main, "title"));
	ib->id = astrdup(j->a, json_str(main, "fullurl"));
	ib->description = astrdup(j->a, json_str(main, "description"));
	ib->content = (char *)first_paragraph(j, json_str(main, "extract"));
	ib->img_src = astrdup(j->a, json_str(yyjson_obj_get(main, "thumbnail"), "source"));
	infobox_url(j, ib, "Wikipedia", ib->id, NULL, 0);

	// "also refers to": the other pages with this name first, then related ones
	for (i = 0; i < n && also < MAX_ALSO; i++)
		if (hits[i] != main && same_base(json_str(hits[i], "title"), title)) {
			infobox_also(j, ib, json_str(hits[i], "title"), json_str(hits[i], "description"));
			also++;
		}
	for (i = 0; i < n && also < MAX_ALSO; i++)
		if (hits[i] != main && !same_base(json_str(hits[i], "title"), title)) {
			infobox_also(j, ib, json_str(hits[i], "title"), json_str(hits[i], "description"));
			also++;
		}
	job_done(j);
}

static void
search_meanings(struct job *j, const char *title)
{
	struct buf q = {0}, words = {0};
	char *t = astrdup(j->a, title), *w, *save;
	struct fetch *f;

	// every word has to be in the title, and pages that are themselves
	// disambiguation boxes (Dmbox) do not count
	for (w = strtok_r(t, " ", &save); w; w = strtok_r(NULL, " ", &save))
		buf_printf(&words, "intitle:%s ", w);
	form_add(&q, "action", "query");
	form_add(&q, "format", "json");
	form_add(&q, "formatversion", "2");
	form_add(&q, "generator", "search");
	form_add(&q, "gsrsearch", aprintf(j->a, "%s-hastemplate:Dmbox", buf_str(&words)));
	buf_free(&words);
	form_add(&q, "gsrlimit", "10");
	form_add(&q, "prop", "extracts|pageimages|description|pageprops|info|pageviews");
	form_add(&q, "pvipdays", "7");
	form_add(&q, "exintro", "1");
	form_add(&q, "explaintext", "1");
	form_add(&q, "exlimit", "10");
	form_add(&q, "piprop", "thumbnail");
	form_add(&q, "pithumbsize", "500");
	form_add(&q, "pilimit", "10");
	form_add(&q, "ppprop", "disambiguation");
	form_add(&q, "inprop", "url");
	f = job_request(j, aprintf(j->a, "https://%s/w/api.php?%s", j->filters->locale->wiki_netloc, buf_str(&q)));
	buf_free(&q);
	job_send(j, f, meanings);
}

static void
summary(struct job *j, struct fetch *f)
{
	const char *title, *link, *type;
	yyjson_val *root;
	struct infobox *ib;
	yyjson_doc *doc;

	if (!job_ok(j, f))
		return;
	if (f->status == 404) {
		// no article with that title, not an error
		job_done(j);
		return;
	}
	doc = job_json(j, f->body.p, f->body.len);
	root = yyjson_doc_get_root(doc);
	if (f->status == 400 && root &&
	    !strcmp(json_str(root, "type"), "https://mediawiki.org/wiki/HyperSwitch/errors/bad_request") &&
	    !strcmp(json_str(root, "detail"), "title-invalid-characters")) {
		// the query could not be a title (a sentence with odd characters)
		job_done(j);
		return;
	}
	if (!job_http_ok(j, f))
		return;
	if (!root || !*(link = json_str(yyjson_ptr_get(root, "/content_urls/desktop"), "page"))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	title = json_str(root, "title");
	type = json_str(root, "type");
	if (!strcmp(type, "disambiguation")) {
		// it could mean many things, find them
		search_meanings(j, title);
		return;
	}
	if (strcmp(type, "standard")) {
		// no_extract, mainpage and so on
		job_done(j);
		return;
	}
	ib = add_infobox(j, html_to_text(j->a, title));
	ib->id = astrdup(j->a, link);
	ib->description = astrdup(j->a, json_str(root, "description"));
	ib->content = astrdup(j->a, json_str(root, "extract"));
	ib->img_src = astrdup(j->a, json_str(yyjson_obj_get(root, "thumbnail"), "source"));
	infobox_url(j, ib, "Wikipedia", link, NULL, 0);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf t = {0};
	const char *q = j->query;
	struct fetch *f;

	// "albert einstein" is not a title, "Albert Einstein" is
	if (py_islower(q))
		q = py_title(j->a, q);
	url_quote(&t, q, "/", 0);
	f = job_request(j,
	    aprintf(j->a, "https://%s/api/rest_v1/page/summary/%s", j->filters->locale->wiki_netloc, buf_str(&t)));
	buf_free(&t);
	// the api redirects from the title we guessed to the real one
	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(f->h, CURLOPT_MAXREDIRS, 2L);
	job_send(j, f, summary);
}

struct engine engine_wikipedia = {
    .name = "wikipedia",
    .shortcut = "wp",
    .website = "www.wikipedia.org",
    .bang = "https://en.wikipedia.org/wiki/Special:Search?search=%s",
    .category = "general",
    .search = search,
};
