#include <string.h>
#include <time.h>

#include "engines/engine.h"
#include "engines/tokens.h"

#define PAGE_SIZE 20

//
// Google images through the programmable search element
//
// the same api and the same token as google_cse.c, with searchtype=image.
// the token is shared (see tokens.c), a string holding the cse_token, the
// library version and the experiment ids, one per line.
//

static void
image_item(struct job *j, yyjson_val *item)
{
	const char *url = json_str(item, "originalContextUrl"), *w, *h, *fmt;
	struct main_result *m;

	// the page the image sits on. without one there is nothing to link to
	if (!*url)
		return;
	m = add_image(j, url, json_str(item, "titleNoFormatting"), json_str(item, "unescapedUrl"));
	m->content = astrdup(j->a, json_str(item, "contentNoFormatting"));
	m->thumbnail_src = astrdup(j->a, json_str(item, "tbUrl"));
	w = json_text(j->a, item, "width");
	h = json_text(j->a, item, "height");
	if (*w && *h)
		m->resolution = aprintf(j->a, "%sx%s", w, h);
	// the format comes as image/png, keep the png
	fmt = strrchr(json_str(item, "fileFormat"), '/');
	m->img_format = astrdup(j->a, fmt ? fmt + 1 : json_str(item, "fileFormat"));
}

static void
response(struct job *j, struct fetch *f)
{
	const char *body, *start, *end;
	yyjson_doc *doc;
	yyjson_val *root, *err, *item, *results;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;

	//
	// Get the json
	//
	// it comes wrapped in a call to the callback we named ("_"), cut that off
	//
	body = fetch_body(f);
	start = strchr(body, '{');
	end = strrchr(body, '}');
	if (!start || !end || end < start || !(doc = job_json(j, start, end - start + 1))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	root = yyjson_doc_get_root(doc);

	//
	// Detect errors
	//
	if ((err = yyjson_obj_get(root, "error"))) {
		const char *msg = yyjson_get_str(yyjson_obj_get(err, "message"));
		j->detail = msg ? msg : "unknown error";
		if (yyjson_get_int(yyjson_obj_get(err, "code")) == 429)
			job_fail(j, "too many requests", suspend_too_many_requests);
		else
			job_fail(j, "server API error", SUSPEND_NONE);
		return;
	}

	//
	// Get results
	//
	results = yyjson_obj_get(root, "results");
	yyjson_arr_foreach (results, i, n, item)
		image_item(j, item);
	job_done(j);
}

static void
request(struct job *j, const char *token)
{
	static const char *safe[] = {"off", "medium", "high"};
	const struct locale *l = j->filters->locale;
	struct buf q = {0};
	struct fetch *f;
	const char *tok, *libv, *exp;
	char page[16], from[16], to[16];
	time_t now = time(NULL), then;

	// split the cached string back into its three parts
	tok = token;
	libv = strchr(tok, '\n') + 1;
	exp = strchr(libv, '\n') + 1;
	form_add(&q, "rsz", "filtered_cse");
	form_add(&q, "num", "20");
	form_add(&q, "hl", l->google_hl);
	form_add(&q, "cselibv", astrndup(j->a, libv, exp - libv - 1));
	form_add(&q, "cx", GOOGLE_CSE_CX);
	form_add(&q, "q", j->query);
	form_add(&q, "safe", safe[j->filters->safesearch]);
	form_add(&q, "cse_tok", astrndup(j->a, tok, libv - tok - 1));
	form_add(&q, "callback", "_");
	form_add(&q, "rurl", "");
	form_add(&q, "searchtype", "image");
	if (j->filters->time_range) {
		then = now - time_range_days(j->filters->time_range) * 86400;
		strftime(from, sizeof(from), "%Y%m%d", localtime(&then));
		strftime(to, sizeof(to), "%Y%m%d", localtime(&now));
		form_add(&q, "sort", aprintf(j->a, "date:r:%s:%s", from, to));
	}
	if (*l->google_gl)
		form_add(&q, "gl", l->google_gl);
	if (*exp)
		form_add(&q, "exp", exp);
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", (j->pageno - 1) * PAGE_SIZE);
		form_add(&q, "start", page);
	}
	f = job_request(j, aprintf(j->a, "https://cse.google.com/cse/element/v1?%s", buf_str(&q)));
	buf_free(&q);
	// agree to the cookie banner up front or google redirects to it
	fetch_cookie(f, "CONSENT", "YES+");
	fetch_header(f, "Accept: */*");
	fetch_header(f, "Referer: https://cse.google.com/");
	f->http_version = CURL_HTTP_VERSION_3;
	job_send(j, f, response);
}

static void
init(void)
{
	token_warm(&google_cse_token);
}

static void
search(struct job *j)
{
	token_get(j, &google_cse_token, request);
}

struct engine engine_google_cse_images = {
    .name = "google cse images",
    .shortcut = "goci",
    .website = "cse.google.com",
    .safesearch = 1,
    .bang = "https://www.google.com/search?tbm=isch&q=%s",
    .category = "images",
    .paging = 1,
    .max_page = 5,
    .time_range = 1,
    .init = init,
    .search = search,
};
