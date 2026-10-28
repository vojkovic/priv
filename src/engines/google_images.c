#include <string.h>

#include "engines/engine.h"
#include "engines/google.h"

//
// Google images, the same basic html as google.c
//
// the same ad preview flags as google.c, with the opera string for images
// (google.h). each picture is a tile with everything we need in its data-
// attributes, the tile's own link is a redirect we ignore.
//

/* the request worked and we were not sent to the captcha */
static int
ok(struct job *j, struct fetch *f)
{
	struct url u;

	if (!job_http_ok(j, f))
		return 0;
	url_parse(j->a, fetch_url(f), &u);
	if (!strcmp(u.netloc, "sorry.google.com") || starts_with(u.path, "/sorry") || f->status == 302 ||
	    (f->body.len < 2000 && strstr(fetch_body(f), "/sorry/"))) {
		job_fail(j, "CAPTCHA", suspend_captcha);
		return 0;
	}
	return 1;
}

static void
response(struct job *j, struct fetch *f)
{
	struct main_result *m;
	struct xnodes tiles;
	htmlDocPtr doc;
	xmlNodePtr n;
	char *w, *h, *tbnid;
	int i;

	//
	// Detect captcha
	//
	if (!ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}

	//
	// Get results
	//
	// ou is the picture, ru the page it is on, pt its title, ow and oh its size
	//
	tiles = xpath((xmlNodePtr)doc, "//div[contains(@class, \"islrtb\") and @data-ou and @data-ru]");
	for (i = 0; i < tiles.n; i++) {
		n = tiles.v[i];
		m = add_image(j, xpath_text(j->a, n, "@data-ru"), xpath_text(j->a, n, "@data-pt"),
		    xpath_text(j->a, n, "@data-ou"));
		// the small version lives on google's own thumbnail server
		if (*(tbnid = xpath_text(j->a, n, "@data-tbnid")))
			m->thumbnail_src = aprintf(j->a, "https://encrypted-tbn0.gstatic.com/images?q=tbn:%s", tbnid);
		w = xpath_text(j->a, n, "@data-ow");
		h = xpath_text(j->a, n, "@data-oh");
		if (*w && *h)
			m->resolution = aprintf(j->a, "%s x %s", w, h);
	}
	xnodes_free(&tiles);
	job_done(j);
}

static void
search(struct job *j)
{
	const struct locale *l = j->filters->locale;
	struct buf q = {0};
	struct fetch *f;
	const char *t;
	char page[16];

	form_add(&q, "q", j->query);
	form_add(&q, "sca_esv", "1");
	form_add(&q, "hl", l->google_hl);
	form_add(&q, "ie", "utf8");
	form_add(&q, "oe", "utf8");
	form_add(&q, "tbm", "isch");
	// ijn is the page number, counting from 0
	if (j->pageno > 1) {
		snprintf(page, sizeof(page), "%d", j->pageno - 1);
		form_add(&q, "ijn", page);
	}
	if ((t = time_range_code(j->filters, "d", "w", "m", "y")))
		form_add(&q, "tbs", aprintf(j->a, "qdr:%s", t));
	if (j->filters->safesearch)
		form_add(&q, "safe", "active");
	f = job_request(j, aprintf(j->a, "https://www.google.com/search?%s%s", buf_str(&q), GOOGLE_ADTEST_IMAGES));
	buf_free(&q);
	job_send(j, f, response);
}

struct engine engine_google_images = {
    .name = "google images",
    .shortcut = "goi",
    .website = "images.google.com",
    .safesearch = 1,
    .bang = "https://www.google.com/search?tbm=isch&q=%s",
    .category = "images",
    .paging = 1,
    .max_page = 50,
    .time_range = 1,
    .search = search,
};
