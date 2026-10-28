#include "botdetection/botdetect.h"
#include "engines/engine.h"
#include "features/autocomplete.h"
#include "features/favicon.h"
#include "features/proxy.h"
#include "features/stats.h"
#include "render/icons.h"
#include "render/internal.h"
#include "render/render.h"
#include "version.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* each site's favicon is inlined once, in a sprite at the end of the page, and referenced from there */
struct favicon {
	const char *authority, *data;
	uint32_t id;
};

static struct favicon favicons[128];
static size_t nfavicons, ninlined;

static int
word_start(const char *s, const char *p)
{
	return p == s || !IS_ALNUM(p[-1]);
}

static void
highlight(struct buf *b, const char *content, const char *query)
{
	struct arena a = {0};
	char *words = astrdup(&a, query), *w, *save, *mark, *src, *d;
	size_t n = MIN(strlen(content), 1024), i, wl;
	const char *p;

	src = astrndup(&a, content, n);
	mark = amalloc(&a, n + 1);
	memset(mark, 0, n + 1);
	for (w = strtok_r(words, " \t\n", &save); w; w = strtok_r(NULL, " \t\n", &save)) {
		for (d = w, p = w; *p; p++)
			if (*p != '\'' && *p != '"')
				*d++ = *p;
		*d = 0;
		if (!(wl = strlen(w)))
			continue;
		for (p = src; (size_t)(p - src) + wl <= n; p++)
			if (!strncasecmp(p, w, wl) && word_start(src, p))
				memset(mark + (p - src), 1, wl);
	}
	for (i = 0; i < n;) {
		size_t j = i;
		while (j < n && mark[j] == mark[i])
			j++;
		if (mark[i])
			buf_puts(b, "<span class=\"highlight\">");
		html_escape(b, astrndup(&a, src + i, j - i));
		if (mark[i])
			buf_puts(b, "</span>");
		i = j;
	}
	arena_free(&a);
}

static void
pretty_url(struct buf *b, struct main_result *m)
{
	struct arena a = {0};
	char *path = astrdup(&a, m->parsed.path), *seg, *save;
	size_t n = strlen(path);

	if (n && path[n - 1] == '/')
		path[n - 1] = 0;
	buf_html(b, "<span class=\"url_host\">%s://%s</span>", m->parsed.scheme, m->parsed.netloc);
	path = url_decode(path);
	for (seg = strtok_r(path, "/", &save); seg; seg = strtok_r(NULL, "/", &save))
		buf_html(b, " &rsaquo; %s", seg);
	arena_free(&a);
}

/* the engines behind a result, as pills, or joined by sep when there is one */
static void
engine_names(struct buf *b, struct main_result *m, const char *sep)
{
	size_t i;
	int first = 1;

	for (i = 0; i < nengines; i++) {
		if (!(m->engines & ENGINE_BIT(engines[i])))
			continue;
		if (*sep)
			buf_html(b, "%r%s", first ? "" : sep, engines[i]->name);
		else
			buf_html(b, "<span>%s</span>", engines[i]->name);
		first = 0;
	}
}

static void
sub_header(struct buf *b, struct main_result *m)
{
	if (!(m->pubdate && *m->pubdate) && !(m->length && *m->length) && !(m->author && *m->author) &&
	    !(m->views && *m->views))
		return;
	buf_puts(b, "<div class=\"result_sub_header\">");
	if (m->pubdate && *m->pubdate)
		buf_html(b, "<time class=\"published_date\">%s</time>", m->pubdate);
	if (m->length && *m->length)
		buf_html(b, "<span>%s: %s</span>", tr("Length"), m->length);
	if (m->views && *m->views)
		buf_html(b, "<span>%s: %s</span>", tr("Views"), m->views);
	if (m->author && *m->author)
		buf_html(b, "<span>%s: %s</span>", tr("Author"), m->author);
	buf_puts(b, "</div>\n");
}

void
attributes(struct buf *b, struct main_result *m)
{
	struct result_attr *at;

	if (!m->attrs && !(m->pdf_url && *m->pdf_url))
		return;
	buf_puts(b, "<div class=\"attributes\">\n");
	for (at = m->attrs; at; at = at->next) {
		if (!strcmp(at->label, "DOI"))
			buf_html(b,
			    "<div><span>%s:</span> <a href=\"https://oadoi.org/%s\" rel=\"noreferrer\">%s</a></div>\n",
			    at->label, at->value, at->value);
		else
			buf_html(b, "<div><span>%s:</span> %s</div>\n", tr(at->label), at->value);
	}
	if (m->pdf_url && *m->pdf_url)
		buf_html(b, "<div class=\"links\"><a href=\"%s\" rel=\"noreferrer\">PDF</a></div>\n", m->pdf_url);
	buf_puts(b, "</div>\n");
}

const char *
image_src(struct search *s, const char *url, const char *proxied, size_t max)
{
	char *inlined = image_inline(&s->a, url, max);

	return inlined ? inlined : proxied;
}

static uint32_t
favicon_id(const char *authority)
{
	uint32_t h = 2166136261u;

	for (; *authority; authority++)
		h = (h ^ (unsigned char)*authority) * 16777619u;
	return h;
}

void
favicon(struct buf *b, struct search *s, const char *authority)
{
	struct favicon *f = NULL;
	size_t i;

	if (!authority || !*authority)
		return;
	for (i = 0; i < nfavicons && !f; i++)
		if (!strcmp(favicons[i].authority, authority))
			f = &favicons[i];
	if (!f && nfavicons < LEN(favicons)) {
		f = &favicons[nfavicons++];
		f->authority = authority;
		f->data = ninlined < FAVICON_INLINE_SITES ?
		    favicon_inline(&s->a, s->filters.prefs.favicon, authority, FAVICON_INLINE_MAX) :
		    NULL;
		ninlined += f->data != NULL;
		f->id = favicon_id(authority);
	}
	if (f && f->data)
		buf_printf(b,
		    "<svg class=\"fav\" viewBox=\"0 0 16 16\" aria-hidden=\"true\"><use href=\"#fav%08x\"/></svg>",
		    f->id);
	else
		buf_html(b, "<img src=\"%s\" alt=\"\" loading=\"lazy\">",
		    favicon_url(&s->a, authority, s->filters.prefs.favicon));
}

static void
favicon_sprite(struct buf *b, struct arena *a)
{
	size_t i;

	buf_puts(b, "<svg class=\"sprite\" aria-hidden=\"true\"><defs>");
	for (i = 0; i < nfavicons; i++)
		if (favicons[i].data)
			buf_html(b, "<image id=\"fav%r\" width=\"16\" height=\"16\" href=\"%s\"/>",
			    aprintf(a, "%08x", favicons[i].id), favicons[i].data);
	buf_puts(b, "</defs></svg>\n");
	nfavicons = ninlined = 0;
}

static void
thumbnail(struct buf *b, struct search *s, struct main_result *m, const char *thumb, int video)
{
	buf_html(b,
	    "<a class=\"thumbnail_link\" href=\"%s\" rel=\"noreferrer\">"
	    "<img class=\"thumbnail\" src=\"%s\" alt=\"\" loading=\"lazy\">",
	    m->url, image_src(s, m->thumbnail, thumb, INLINE_MAX));
	if (video && m->length && *m->length)
		buf_html(b, "<span class=\"thumbnail_length\">%s</span>", m->length);
	buf_puts(b, "</a>\n");
}

void
content(struct buf *b, struct search *s, struct main_result *m)
{
	if (m->content[0]) {
		buf_puts(b, "<p class=\"content\">");
		highlight(b, m->content, s->query);
		buf_puts(b, "</p>\n");
	} else if (!m->attrs) {
		buf_html(b, "<p class=\"content empty_element\">%s</p>\n",
		    tr("This site did not provide any description."));
	}
}

static void
main_result(struct buf *b, struct search *s, struct main_result *m)
{
	char *thumb = image_url(&s->a, m->thumbnail);
	int video = !strcmp(m->template, "videos");

	buf_html(b, "<article class=\"result result-%s\">\n", m->template);
	if (thumb && !video)
		thumbnail(b, s, m, thumb, 0);

	buf_html(b,
	    "<a class=\"url_header\" href=\"%s\" rel=\"noreferrer\">"
	    "<span class=\"favicon\">",
	    m->url);
	favicon(b, s, m->parsed.netloc);
	buf_puts(b,
	    "</span>"
	    "<span class=\"url_wrapper\">");
	pretty_url(b, m);
	buf_html(b,
	    "</span>"
	    "</a>\n"
	    "<h3><a href=\"%s\" rel=\"noreferrer\">%s</a></h3>\n",
	    m->url, m->title[0] ? m->title : m->url);

	if (video) {
		buf_puts(b, "<div class=\"video_body\">\n");
		if (thumb)
			thumbnail(b, s, m, thumb, 1);
		buf_puts(b, "<div class=\"video_text\">\n");
	}
	sub_header(b, m);
	content(b, s, m);
	if (video)
		buf_puts(b,
		    "</div>\n"
		    "</div>\n");

	attributes(b, m);
	buf_puts(b, "<div class=\"engines\">");
	engine_names(b, m, "");
	buf_puts(b,
	    "</div>\n"
	    "</article>\n");
}

static int
resolution(const char *res, long *w, long *h)
{
	char *end;

	*h = 0;
	*w = strtol(res, &end, 10);
	while (*end && !IS_DIGIT(*end))
		end++;
	if (*w > 0)
		*h = strtol(end, NULL, 10);
	return *w > 0 && *h > 0;
}

static double
aspect(struct main_result *m)
{
	long w, h;

	return m->resolution && resolution(m->resolution, &w, &h) ? MIN(MAX((double)w / h, 0.6), 2.2) : 4.0 / 3;
}

static void
image_result(struct buf *b, struct search *s, struct main_result *m)
{
	struct arena *a = &s->a;
	const char *site, *small, *res = "";
	char *full, *thumb, *fav;
	long w, h;

	/* a bang like "!goi !g" mixes in web results, which have nothing to show in the image grid */
	if (!m->img_src || !*m->img_src)
		return;
	site = m->source && *m->source ? m->source : m->parsed.netloc;
	small = m->thumbnail_src && *m->thumbnail_src ? m->thumbnail_src : m->img_src;
	full = image_url(a, m->img_src);
	thumb = image_url(a, small);
	fav = favicon_url(a, m->parsed.netloc, s->filters.prefs.favicon);
	if (!full)
		return;
	if (m->resolution && resolution(m->resolution, &w, &h))
		res = aprintf(a, "%ld \xc3\x97 %ld", w, h);

	/* the data- attributes feed the detail panel priv.js opens */
	buf_html(b,
	    "<article class=\"result result-images s%d\" data-title=\"%s\" data-content=\"%s\" data-url=\"%s\" "
	    "data-site=\"%s\" data-favicon=\"%s\" data-resolution=\"%s\" data-format=\"%s\" data-engines=\"",
	    MIN(MAX((int)(aspect(m) * 8 / 3 + 0.5), 2), 6), m->title, m->content, m->url, site, fav ? fav : "", res,
	    m->img_format);
	engine_names(b, m, ", ");
	buf_html(b,
	    "\">\n"
	    "<a class=\"image_thumb\" href=\"%s\" rel=\"noreferrer\">",
	    full);
	if (thumb)
		buf_html(b, "<img src=\"%s\" alt=\"\" loading=\"lazy\">", image_src(s, small, thumb, INLINE_MAX));
	if (*res)
		buf_html(b, "<span class=\"image_resolution\">%s</span>", res);
	buf_html(b,
	    "</a>\n"
	    "<a class=\"image_source\" href=\"%s\" rel=\"noreferrer\">"
	    "<span class=\"image_site\">",
	    m->url);
	favicon(b, s, m->parsed.netloc);
	buf_html(b,
	    "<span>%s</span>"
	    "</span>"
	    "<span class=\"image_title\">%s</span>"
	    "</a>\n"
	    "</article>\n",
	    site, m->title[0] ? m->title : site);
}

static void
image_detail_template(struct buf *b)
{
	buf_html(b,
	    "<template id=\"image_detail\">"
	    "<aside class=\"image_detail\" aria-label=\"%s\">"
	    "<div class=\"detail_header\">"
	    "<a class=\"detail_site\" rel=\"noreferrer\"><img alt=\"\"><span></span></a>"
	    "<div class=\"detail_buttons\">"
	    "<button type=\"button\" class=\"detail_prev\" aria-label=\"%s\">" ICON_BACK "</button>"
	    "<button type=\"button\" class=\"detail_next\" aria-label=\"%s\">" ICON_FORWARD "</button>"
	    "<button type=\"button\" class=\"detail_close\" aria-label=\"%s\">" ICON_CLOSE "</button>"
	    "</div>"
	    "</div>"
	    "<a class=\"detail_image\" rel=\"noreferrer\"><img alt=\"\"></a>"
	    "<div class=\"detail_body\">"
	    "<h3><a class=\"detail_title\" rel=\"noreferrer\"></a></h3>"
	    "<p class=\"detail_content\"></p>"
	    "<dl data-resolution=\"%s\" data-format=\"%s\" data-engines=\"%s\"></dl>"
	    "</div>"
	    "</aside>"
	    "</template>\n",
	    tr("Image details"), tr("Previous image"), tr("Next image"), tr("Close"), tr("Resolution"), tr("Format"),
	    tr("Engines"));
}

static void
no_results(struct buf *b, struct search *s)
{
	buf_puts(b, "<div class=\"no-results\" role=\"alert\"><p>");
	put_tr(b, s->pageno == 1 ? "No results for {0}" : "No more results for {0}",
	    aprintf(&s->a, "<strong>%s</strong>", html_str(&s->a, s->query)), "");
	buf_puts(b, "</p>");
	if (s->pageno == 1)
		buf_html(b, "<p>%s</p>", tr("Try different words or another category."));
	buf_puts(b, "</div>\n");
}

enum status { OK, TIMEOUT, ERROR, SUSPENDED };

struct row {
	const struct engine *engine;
	double seconds;
	enum status status;
	char *error;
};

static int
row_cmp(const void *x, const void *y)
{
	const struct row *a = x, *b = y;

	if (a->status != b->status)
		return (int)a->status - (int)b->status;
	if (a->seconds != b->seconds)
		return a->seconds < b->seconds ? -1 : 1;
	return strcmp(a->engine->name, b->engine->name);
}

static int
str_cmp(const void *x, const void *y)
{
	return strcmp(*(char *const *)x, *(char *const *)y);
}

/* one row per engine that ran or was skipped, with its errors joined and its status, best first */
static size_t
timing_rows(struct search *s, struct row *rows, size_t *nok, double *max)
{
	struct results *r = &s->res;
	const char **errs = amalloc(&s->a, (r->nunresponsive + 1) * sizeof(*errs));
	size_t i, j, k, n = 0, nerr;
	int suspended;

	*nok = 0;
	*max = 0;
	for (i = 0; i < r->ntimings; i++) {
		rows[n].engine = r->timings[i].engine;
		rows[n].seconds = r->timings[i].seconds;
		rows[n++].status = OK;
		*max = MAX(*max, r->timings[i].seconds);
	}
	for (i = 0; i < r->nunresponsive; i++) {
		for (j = 0; j < n && rows[j].engine != r->unresponsive[i].engine; j++)
			;
		if (j == n) {
			rows[n].engine = r->unresponsive[i].engine;
			rows[n].seconds = 0;
			rows[n++].status = SUSPENDED;
		}
	}
	for (j = 0; j < n; j++) {
		suspended = 0;
		for (i = 0, nerr = 0; i < r->nunresponsive; i++) {
			const char *e = r->unresponsive[i].error;
			if (r->unresponsive[i].engine != rows[j].engine)
				continue;
			suspended |= r->unresponsive[i].suspended;
			for (k = 0; k < nerr && strcmp(errs[k], e); k++)
				;
			if (k == nerr)
				errs[nerr++] = e;
		}
		qsort(errs, nerr, sizeof(*errs), str_cmp);
		rows[j].error = NULL;
		if (nerr) {
			struct buf eb = {0};
			for (k = 0; k < nerr; k++)
				buf_printf(&eb, "%s%s", k ? ", " : "", errs[k]);
			rows[j].error = astrdup(&s->a, buf_str(&eb));
			buf_free(&eb);
			if (rows[j].status == OK)
				rows[j].status = !strcmp(rows[j].error, "timeout") ? TIMEOUT : ERROR;
		}
		if (suspended)
			rows[j].status = SUSPENDED;
		if (rows[j].status == OK)
			(*nok)++;
	}
	qsort(rows, n, sizeof(*rows), row_cmp);
	return n;
}

static void
engine_timing(struct buf *b, struct search *s)
{
	static const char *cls[] = {"ok", "timeout", "error", "suspended"};
	struct results *r = &s->res;
	struct row *rows = amalloc(&s->a, (r->ntimings + r->nunresponsive + 1) * sizeof(*rows));
	double max, limit = filters_timeout(&s->filters);
	size_t i, n, nok;
	int pct;

	if (!(n = timing_rows(s, rows, &nok, &max)))
		return;

	buf_html(b,
	    "<section id=\"engines_msg\" aria-label=\"%s\">\n"
	    "<input type=\"checkbox\" id=\"timings_toggle\" hidden>"
	    "<label for=\"timings_toggle\" class=\"timings_header\">"
	    "<span class=\"count\">",
	    tr("Response time"));
	put_tr(b, "{0} of {1} engines", aprintf(&s->a, "%zu", nok), aprintf(&s->a, "%zu", n));
	buf_html(b,
	    "</span>"
	    "<span class=\"total\">%r<span class=\"limit\"> / %rs</span></span>" ICON_EXPAND "</label>\n"
	    "<ol class=\"timings\">\n",
	    aprintf(&s->a, "%.2f", max), aprintf(&s->a, "%.2f", limit));

	for (i = 0; i < n; i++) {
		pct = rows[i].status == SUSPENDED ? 0 : (int)(100 * rows[i].seconds / limit + 0.5);
		if (rows[i].error)
			buf_html(b, "<li class=\"%r\" title=\"%s\">", cls[rows[i].status], rows[i].error);
		else
			buf_html(b, "<li class=\"%r\">", cls[rows[i].status]);
		buf_puts(b, "<span class=\"label\">");
		favicon(b, s, rows[i].engine->website);
		buf_html(b,
		    "<span class=\"name\">%s</span>"
		    "</span>"
		    "<svg class=\"track\" viewBox=\"0 0 100 2\" preserveAspectRatio=\"none\" aria-hidden=\"true\">"
		    "<rect class=\"bg\" width=\"100\" height=\"2\"/>"
		    "<rect class=\"fill\" width=\"%d\" height=\"2\"/>"
		    "</svg>",
		    rows[i].engine->name, MIN(MAX(pct, 0), 100));
		if (rows[i].status == SUSPENDED)
			buf_puts(b, "<span class=\"time\"></span></li>\n");
		else
			buf_html(b, "<span class=\"time\">%r</span></li>\n", aprintf(&s->a, "%.2f", rows[i].seconds));
	}
	buf_puts(b,
	    "</ol>\n"
	    "</section>\n");
}

static void
token_params(struct buf *b, struct search *s)
{
	struct buf t = {0};
	size_t i;

	for (i = 0; i < s->njobs; i++)
		if (s->jobs[i].next_token)
			form_add(&t, s->jobs[i].e->name, s->jobs[i].next_token);
	if (t.len)
		buf_html(b, "&amp;pt=%u&amp;ph=%s", buf_str(&t), proxy_hmac(&s->a, 'p', buf_str(&t)));
	buf_free(&t);
}

static void
page_link(struct buf *b, struct search *s, int page, const char *label, const char *cls)
{
	buf_html(b, "<a class=\"%r\" href=\"/search?q=%u&amp;p=%d", cls, s->raw, page);
	filter_params(b, &s->filters);
	if (page == s->pageno + 1)
		token_params(b, s);
	buf_html(b, "\">%r</a>", label);
}

static void
pagination(struct buf *b, struct search *s)
{
	int p, start = s->pageno > 5 ? s->pageno - 4 : 1, end = start + 10;

	buf_puts(b, "<nav id=\"pagination\">\n");
	if (s->pageno > 1)
		page_link(b, s, s->pageno - 1, aprintf(&s->a, "&lsaquo; %s", html_str(&s->a, tr("Previous page"))),
		    "previous_page");
	if (s->res.nmain)
		page_link(b, s, s->pageno + 1, aprintf(&s->a, "%s &rsaquo;", html_str(&s->a, tr("Next page"))),
		    "next_page");

	buf_puts(b, "<div class=\"numbered_pagination\">");
	for (p = start; p < end; p++) {
		if (p > s->pageno && !s->res.nmain)
			break;
		if (p == s->pageno) {
			buf_html(b, "<span class=\"page_number_current\">%d</span>", p);
			continue;
		}
		buf_html(b, "<a class=\"page_number\" href=\"/search?q=%u&amp;p=%d", s->raw, p);
		filter_params(b, &s->filters);
		if (p == s->pageno + 1)
			token_params(b, s);
		buf_html(b, "\">%d</a>", p);
	}
	buf_puts(b,
	    "</div>\n"
	    "</nav>\n");
}

/* the rest of a results page, after render_head has been sent and the engines have answered */
void
render_results(struct buf *b, struct search *s)
{
	struct results *r = &s->res;
	int images = !strcmp(s->filters.category, "images");
	size_t i;

	i18n_use(s->filters.prefs.ui);
	if (s->pageno == 1 && !strcmp(s->filters.category, "general") && self_query(s->raw))
		selfinfo(b, &s->filters);
	answers(b, s);

	/* images use the full width, so their timings take a grid slot instead of the sidebar */
	if (!images) {
		buf_puts(b, "<div id=\"sidebar\">\n");
		if (r->ninfoboxes) {
			buf_puts(b, "<div id=\"infoboxes\">\n");
			infobox(b, s, shown_infobox(r));
			buf_puts(b, "</div>\n");
		}
		engine_timing(b, s);
		buf_puts(b, "</div>\n");
	}

	buf_puts(b, "<div id=\"urls\" role=\"main\">\n");
	if (images)
		engine_timing(b, s);
	for (i = 0; i < r->nmain; i++) {
		if (images)
			image_result(b, s, r->main[i]);
		else
			main_result(b, s, r->main[i]);
	}
	if (!r->nmain && !r->nanswers)
		no_results(b, s);
	buf_puts(b, "</div>\n");

	if (r->paging)
		pagination(b, s);
	buf_puts(b, "</div>\n");
	if (images)
		image_detail_template(b);
	favicon_sprite(b, &s->a);
	buf_puts(b,
	    "</body>\n"
	    "</html>\n");
}
