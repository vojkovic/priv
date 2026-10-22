#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "botdetect.h"
#include "engine.h"
#include "icons.h"
#include "proxy.h"
#include "render.h"

#define LOGO \
	"<svg class=\"logo\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 92 92\" aria-hidden=\"true\"><g " \
	"transform=\"translate(-40.921 -17.417)\"><circle cx=\"75.921\" cy=\"53.903\" r=\"30\" fill=\"none\" " \
	"stroke=\"currentColor\" stroke-width=\"10\"/><path d=\"M67.515 37.915a18 18 0 0 1 21.051 3.313 18 18 0 0 1 " \
	"3.138 21.078\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"5\"/><rect width=\"18.846\" " \
	"height=\"39.963\" x=\"3.706\" y=\"122.09\" transform=\"rotate(-46.235)\" fill=\"currentColor\"/></g></svg>"

static void
doc_start(struct buf *b, const char *title, const char *body_class)
{
	buf_html(b,
	    "<!doctype html>\n"
	    "<html lang=\"en\">\n"
	    "<head>\n"
	    "<meta charset=\"utf-8\">\n"
	    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
	    "<meta name=\"referrer\" content=\"no-referrer\">\n"
	    "<title>%s</title>\n"
	    "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">\n"
	    "<link rel=\"stylesheet\" href=\"/style.css\">\n"
	    "<link rel=\"search\" type=\"application/opensearchdescription+xml\" title=\"privy\" href=\"/opensearch.xml\">\n"
	    "<script src=\"/privy.js\" defer></script>\n",
	    title);
	if (limiter && limiter_link_token)
		buf_html(b, "<link rel=\"stylesheet\" href=\"/client%s.css\">\n", botdetect_token());
	buf_html(b, "</head>\n<body class=\"%s\">\n", body_class);
}

static void
search_box(struct buf *b, const char *query, const char *category, int autofocus)
{
	buf_html(b,
	    "<div id=\"search_view\"><div class=\"search_box\">"
	    "<input id=\"q\" type=\"search\" name=\"q\" value=\"%s\" placeholder=\"Search for...\" aria-label=\"Search\" "
	    "autocomplete=\"off\" autocapitalize=\"none\" spellcheck=\"false\" required%r>"
	    "<button id=\"send_search\" type=\"submit\" name=\"categories\" value=\"%s\" aria-label=\"Search\">" ICON_SEARCH
	    "</button>"
	    "<div class=\"autocomplete\"><ul></ul></div>"
	    "</div></div>\n",
	    query, autofocus ? " autofocus" : "", category);
}

static void
category_tabs(struct buf *b, const char *current)
{
	size_t i;

	buf_puts(b, "<div id=\"categories\"><div id=\"categories_container\">");
	for (i = 0; i < ncategories; i++)
		buf_html(b,
		    "<button type=\"submit\" name=\"categories\" value=\"%s\" class=\"category_button%r\" "
		    "aria-label=\"%s\">%r<span class=\"category_name\">%s</span></button>",
		    categories[i].name, strcmp(categories[i].name, current) ? "" : " selected", categories[i].label,
		    categories[i].icon, categories[i].label);
	buf_puts(b, "</div></div>\n");
}

static void
option(struct buf *b, const char *value, const char *label, int selected)
{
	buf_html(b, "<option value=\"%s\"%r>%s</option>", value, selected ? " selected" : "", label);
}

static void
search_filters(struct buf *b, const struct filters *f)
{
	static const char *ranges[][2] = {
	    {"", "Anytime"},
	    {"day", "Last day"},
	    {"week", "Last week"},
	    {"month", "Last month"},
	    {"year", "Last year"},
	};
	static const char *safe[] = {"SafeSearch: None", "SafeSearch: Moderate", "SafeSearch: Strict"};
	size_t i;

	buf_puts(b,
	    "<div class=\"search_filters\">\n<select name=\"language\" form=\"search\" aria-label=\"Search language\">");
	option(b, locales[0].tag, locales[0].label, !strcmp(f->language, "all"));
	buf_html(b, "<option value=\"auto\"%r>Auto-detect (%s)</option>",
	    !strcmp(f->language, "auto") ? " selected" : "", f->locale->tag);
	for (i = 1; i < nlocales; i++)
		option(b, locales[i].tag, locales[i].label, !strcmp(f->language, locales[i].tag));
	buf_puts(b, "</select>\n<select name=\"time_range\" form=\"search\" aria-label=\"Time range\">");
	for (i = 0; i < LEN(ranges); i++)
		option(b, ranges[i][0], ranges[i][1], f->time_range ? !strcmp(f->time_range, ranges[i][0]) : !i);
	buf_puts(b, "</select>\n<select name=\"safesearch\" form=\"search\" aria-label=\"SafeSearch\">");
	for (i = 3; i-- > 0;)
		option(b, (const char *[]){"0", "1", "2"}[i], safe[i], f->safesearch == (int)i);
	buf_puts(b, "</select>\n</div>\n");
}

static void
filter_params(struct buf *b, const struct filters *f)
{
	if (strcmp(f->category, "general"))
		buf_html(b, "&amp;categories=%u", f->category);
	if (strcmp(f->language, "en"))
		buf_html(b, "&amp;language=%u", f->language);
	if (f->time_range)
		buf_html(b, "&amp;time_range=%u", f->time_range);
	if (f->safesearch)
		buf_html(b, "&amp;safesearch=%d", f->safesearch);
}

void
render_home(struct buf *b)
{
	doc_start(b, "privy", "index_endpoint");
	buf_puts(b,
	    "<main id=\"main_index\">\n<h1 class=\"title\">privy</h1>\n"
	    "<form id=\"search\" action=\"/search\" method=\"get\" role=\"search\">\n");
	search_box(b, "", "general", 1);
	buf_puts(b, "</form>\n</main>\n</body>\n</html>\n");
}

void
render_head(struct buf *b, struct search *s)
{
	doc_start(b,
	    s->pageno > 1 ? aprintf(&s->a, "%s (page %d) - privy", s->raw, s->pageno) :
	                    aprintf(&s->a, "%s - privy", s->raw),
	    "results_endpoint");
	buf_puts(b,
	    "<header id=\"search_header\">\n<a id=\"search_logo\" href=\"/\" aria-label=\"privy\">" LOGO "</a>\n"
	    "<form id=\"search\" action=\"/search\" method=\"get\" role=\"search\">\n");
	search_box(b, s->raw, s->filters.category, 0);
	category_tabs(b, s->filters.category);
	buf_html(b,
	    "</form>\n</header>\n<div class=\"loading t%d\" role=\"progressbar\" aria-label=\"Searching\"></div>\n",
	    MIN(MAX((int)((s->filters.timeout > 0 ? s->filters.timeout : search_timeout) + 0.99), 1), 10));
	search_filters(b, &s->filters);
	buf_html(b, "<div id=\"results\" class=\"category-%s\">\n", s->filters.category);
}

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
		buf_html(b, "<span>Length: %s</span>", m->length);
	if (m->views && *m->views)
		buf_html(b, "<span>Views: %s</span>", m->views);
	if (m->author && *m->author)
		buf_html(b, "<span>Author: %s</span>", m->author);
	buf_puts(b, "</div>\n");
}

static void
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
			buf_html(b, "<div><span>%s:</span> %s</div>\n", at->label, at->value);
	}
	if (m->pdf_url && *m->pdf_url)
		buf_html(b, "<div class=\"links\"><a href=\"%s\" rel=\"noreferrer\">PDF</a></div>\n", m->pdf_url);
	buf_puts(b, "</div>\n");
}

static void
thumbnail(struct buf *b, struct main_result *m, const char *thumb, int video)
{
	buf_html(b,
	    "<a class=\"thumbnail_link\" href=\"%s\" rel=\"noreferrer\">"
	    "<img class=\"thumbnail_bg\" src=\"%s\" alt=\"\" aria-hidden=\"true\">"
	    "<img class=\"thumbnail\" src=\"%s\" alt=\"\" loading=\"lazy\">",
	    m->url, thumb, thumb);
	if (video && m->length && *m->length)
		buf_html(b, "<span class=\"thumbnail_length\">%s</span>", m->length);
	buf_puts(b, "</a>\n");
}

static void
content(struct buf *b, struct search *s, struct main_result *m)
{
	if (m->content[0]) {
		buf_puts(b, "<p class=\"content\">");
		highlight(b, m->content, s->query);
		buf_puts(b, "</p>\n");
	} else if (!m->attrs) {
		buf_puts(b, "<p class=\"content empty_element\">This site did not provide any description.</p>\n");
	}
}

static void
main_result(struct buf *b, struct search *s, struct main_result *m)
{
	struct arena *a = &s->a;
	char *thumb = image_url(a, m->thumbnail), *fav = favicon_url(a, m->parsed.netloc);
	int video = !strcmp(m->template, "videos");

	buf_html(b, "<article class=\"result result-%s\">\n", m->template);
	if (thumb && !video)
		thumbnail(b, m, thumb, 0);
	buf_html(b, "<a class=\"url_header\" href=\"%s\" rel=\"noreferrer\">", m->url);
	if (fav)
		buf_html(b, "<span class=\"favicon\"><img src=\"%s\" alt=\"\" loading=\"lazy\"></span>", fav);
	buf_puts(b, "<span class=\"url_wrapper\">");
	pretty_url(b, m);
	buf_html(b, "</span></a>\n<h3><a href=\"%s\" rel=\"noreferrer\">%s</a></h3>\n", m->url,
	    m->title[0] ? m->title : m->url);
	if (video) {
		buf_puts(b, "<div class=\"video_body\">\n");
		if (thumb)
			thumbnail(b, m, thumb, 1);
		buf_puts(b, "<div class=\"video_text\">\n");
	}
	sub_header(b, m);
	content(b, s, m);
	if (video)
		buf_puts(b, "</div>\n</div>\n");
	attributes(b, m);
	buf_puts(b, "<div class=\"engines\">");
	engine_names(b, m, "");
	buf_puts(b, "</div>\n</article>\n");
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

static void
image_result(struct buf *b, struct search *s, struct main_result *m)
{
	struct arena *a = &s->a;
	const char *site = *m->source ? m->source : m->parsed.netloc, *res = "";
	int span;
	char *full = image_url(a, m->img_src), *thumb = image_url(a, *m->thumbnail_src ? m->thumbnail_src : m->img_src);
	char *fav = favicon_url(a, m->parsed.netloc);
	double r = 4.0 / 3;
	long w, h;

	if (!full)
		return;
	if (resolution(m->resolution, &w, &h)) {
		res = aprintf(a, "%ld \xc3\x97 %ld", w, h);
		r = MIN(MAX((double)w / h, 0.6), 2.2);
	}
	span = MIN(MAX((int)(r * 8 / 3 + 0.5), 2), 6);
	buf_html(b,
	    "<article class=\"result result-images s%d\" data-title=\"%s\" data-content=\"%s\" "
	    "data-url=\"%s\" data-site=\"%s\" data-favicon=\"%s\" data-resolution=\"%s\" data-format=\"%s\" "
	    "data-engines=\"",
	    span, m->title, m->content, m->url, site, fav ? fav : "", res, m->img_format);
	engine_names(b, m, ", ");
	buf_html(b, "\">\n<a class=\"image_thumb\" href=\"%s\" rel=\"noreferrer\">", full);
	if (thumb)
		buf_html(b, "<img src=\"%s\" alt=\"\" loading=\"lazy\">", thumb);
	if (*res)
		buf_html(b, "<span class=\"image_resolution\">%s</span>", res);
	buf_html(b, "</a>\n<a class=\"image_source\" href=\"%s\" rel=\"noreferrer\"><span class=\"image_site\">",
	    m->url);
	if (fav)
		buf_html(b, "<img src=\"%s\" alt=\"\" loading=\"lazy\">", fav);
	buf_html(b, "<span>%s</span></span><span class=\"image_title\">%s</span></a>\n</article>\n", site,
	    m->title[0] ? m->title : site);
}

static void
image_detail_template(struct buf *b)
{
	buf_puts(b,
	    "<template id=\"image_detail\"><aside class=\"image_detail\" aria-label=\"Image details\">"
	    "<div class=\"detail_header\"><a class=\"detail_site\" rel=\"noreferrer\"><img alt=\"\"><span></span></a>"
	    "<div class=\"detail_buttons\">"
	    "<button type=\"button\" class=\"detail_prev\" aria-label=\"Previous image\">" ICON_BACK "</button>"
	    "<button type=\"button\" class=\"detail_next\" aria-label=\"Next image\">" ICON_FORWARD "</button>"
	    "<button type=\"button\" class=\"detail_close\" aria-label=\"Close\">" ICON_CLOSE "</button>"
	    "</div></div>"
	    "<a class=\"detail_image\" rel=\"noreferrer\"><img alt=\"\"></a>"
	    "<div class=\"detail_body\"><h3><a class=\"detail_title\" rel=\"noreferrer\"></a></h3>"
	    "<p class=\"detail_content\"></p><dl></dl></div>"
	    "</aside></template>\n");
}

static char *
url_favicon(struct arena *a, const char *url)
{
	struct url u;

	url_parse(a, url, &u);
	return favicon_url(a, u.netloc);
}

static int
in_infobox(struct results *r, const char *url)
{
	struct infobox_url *u;
	size_t i;

	for (i = 0; url && i < r->ninfoboxes; i++)
		for (u = r->infoboxes[i]->urls; u; u = u->next)
			if (u->url && !strcmp(u->url, url))
				return 1;
	return 0;
}

static const char *
source_name(struct arena *a, const char *url)
{
	struct url u;

	url_parse(a, url, &u);
	if (strstr(u.netloc, "wikipedia.org"))
		return "Wikipedia";
	return starts_with(u.netloc, "www.") ? u.netloc + 4 : u.netloc;
}

static void
infobox(struct buf *b, struct search *s, struct infobox *ib)
{
	struct infobox_url *u, *source = NULL;
	struct infobox_attr *at;
	char *img = image_url(&s->a, ib->img_src), *fav;
	int chips = 0;

	for (u = ib->urls; u && !source; u = u->next)
		if (u->title && !strcmp(u->title, "Wikipedia"))
			source = u;
	buf_puts(b, "<aside class=\"infobox\">\n");
	if (img)
		buf_html(b, "<img class=\"infobox_image\" src=\"%s\" alt=\"\">\n", img);
	buf_html(b, "<h2 class=\"title\">%s</h2>\n", ib->title);
	if (ib->description && *ib->description)
		buf_html(b, "<p class=\"description\">%s</p>\n", ib->description);
	if (ib->content && *ib->content)
		buf_html(b, "<p class=\"extract\">%s</p>\n", ib->content);
	if (ib->attrs) {
		buf_puts(b, "<dl class=\"attributes\">\n");
		for (at = ib->attrs; at; at = at->next)
			buf_html(b, "<dt>%s</dt><dd>%s</dd>\n", at->label ? at->label : "", at->value);
		buf_puts(b, "</dl>\n");
	}
	if (ib->also) {
		buf_puts(b, "<div class=\"also\"><span class=\"also_label\">Also refers to</span><ul>\n");
		for (u = ib->also; u; u = u->next) {
			buf_html(b, "<li><a href=\"/search?q=%u", u->title);
			filter_params(b, &s->filters);
			buf_html(b, "\">%s</a>", u->title);
			if (u->entity)
				buf_html(b, "<span>%s</span>", u->entity);
			buf_puts(b, "</li>\n");
		}
		buf_puts(b, "</ul></div>\n");
	}
	for (u = ib->urls; u; u = u->next) {
		if (u == source)
			continue;
		if (!chips++)
			buf_puts(b, "<div class=\"links\">");
		fav = url_favicon(&s->a, u->url);
		buf_html(b, "<a href=\"%s\" rel=\"noreferrer\">", u->url);
		if (fav)
			buf_html(b, "<img src=\"%s\" alt=\"\" loading=\"lazy\">", fav);
		buf_html(b, "%s</a>", u->title && *u->title ? u->title : u->url);
	}
	if (chips)
		buf_puts(b, "</div>\n");
	if (source)
		buf_html(b, "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">Wikipedia" ICON_ARROW_NE "</a>\n",
		    source->url);
	buf_puts(b, "</aside>\n");
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

static void
engine_timing(struct buf *b, struct search *s)
{
	static const char *cls[] = {"ok", "timeout", "error", "suspended"};
	struct results *r = &s->res;
	struct row *rows = amalloc(&s->a, (r->ntimings + r->nunresponsive + 1) * sizeof(*rows));
	const char **errs = amalloc(&s->a, (r->nunresponsive + 1) * sizeof(*errs));
	double max = 0, limit = s->filters.timeout > 0 ? s->filters.timeout : search_timeout;
	size_t i, j, k, n = 0, nerr, nok = 0;
	int suspended;

	for (i = 0; i < r->ntimings; i++) {
		rows[n].engine = r->timings[i].engine;
		rows[n].seconds = r->timings[i].seconds;
		rows[n++].status = OK;
		max = MAX(max, r->timings[i].seconds);
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
	if (!n)
		return;
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
			nok++;
	}
	qsort(rows, n, sizeof(*rows), row_cmp);

	buf_html(b,
	    "<section id=\"engines_msg\" aria-label=\"Response time\">\n<header><span class=\"count\">%z of %z engines</span>"
	    "<span class=\"total\">%r<span class=\"limit\"> / %rs</span></span></header>\n<ol class=\"timings\">\n",
	    nok, n, aprintf(&s->a, "%.2f", max), aprintf(&s->a, "%.2f", limit));
	for (i = 0; i < n; i++) {
		int pct = rows[i].status == SUSPENDED ? 0 : (int)(100 * rows[i].seconds / limit + 0.5);
		buf_html(b,
		    "<li class=\"%r\"><span class=\"label\"><img src=\"%s\" alt=\"\" loading=\"lazy\">"
		    "<span class=\"name\">%s</span>",
		    cls[rows[i].status], favicon_url(&s->a, rows[i].engine->website), rows[i].engine->name);
		buf_html(b,
		    "</span><svg class=\"track\" viewBox=\"0 0 100 2\" preserveAspectRatio=\"none\" aria-hidden=\"true\">"
		    "<rect class=\"bg\" width=\"100\" height=\"2\"/><rect class=\"fill\" width=\"%d\" height=\"2\"/></svg>",
		    MIN(MAX(pct, 0), 100));
		if (rows[i].status == SUSPENDED)
			buf_puts(b, "<span class=\"time\"></span></li>\n");
		else
			buf_html(b, "<span class=\"time\">%r</span></li>\n", aprintf(&s->a, "%.2f", rows[i].seconds));
	}
	buf_puts(b, "</ol>\n</section>\n");
}

static void
page_link(struct buf *b, struct search *s, int page, const char *label, const char *cls)
{
	buf_html(b, "<a class=\"%r\" href=\"/search?q=%u&amp;p=%d", cls, s->raw, page);
	filter_params(b, &s->filters);
	buf_html(b, "\">%r</a>", label);
}

static void
pagination(struct buf *b, struct search *s)
{
	int p, start = s->pageno > 5 ? s->pageno - 4 : 1, end = start + 10;

	buf_puts(b, "<nav id=\"pagination\">\n");
	if (s->pageno > 1)
		page_link(b, s, s->pageno - 1, "&lsaquo; Previous page", "previous_page");
	if (s->res.nmain)
		page_link(b, s, s->pageno + 1, "Next page &rsaquo;", "next_page");
	buf_puts(b, "<div class=\"numbered_pagination\">");
	for (p = start; p < end; p++) {
		if (p > s->pageno && !s->res.nmain)
			break;
		if (p == s->pageno)
			buf_html(b, "<span class=\"page_number_current\">%d</span>", p);
		else {
			buf_html(b, "<a class=\"page_number\" href=\"/search?q=%u&amp;p=%d", s->raw, p);
			filter_params(b, &s->filters);
			buf_html(b, "\">%d</a>", p);
		}
	}
	buf_puts(b, "</div>\n</nav>\n");
}

void
render_results(struct buf *b, struct search *s)
{
	struct results *r = &s->res;
	int images = !strcmp(s->filters.category, "images");
	size_t i, n;

	for (i = n = 0; i < r->nanswers; i++)
		if (!in_infobox(r, r->answers[i]->url))
			r->answers[n++] = r->answers[i];
	r->nanswers = n;
	if (r->nanswers) {
		buf_puts(b, "<div id=\"answers\">\n");
		for (i = 0; i < r->nanswers; i++) {
			buf_html(b, "<div class=\"answer\"><p>%s</p>", r->answers[i]->answer);
			if (r->answers[i]->url)
				buf_html(b,
				    "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">%s" ICON_ARROW_NE "</a>",
				    r->answers[i]->url, source_name(&s->a, r->answers[i]->url));
			buf_puts(b, "</div>\n");
		}
		buf_puts(b, "</div>\n");
	}

	if (!images) {
		buf_puts(b, "<div id=\"sidebar\">\n");
		if (r->ninfoboxes) {
			buf_puts(b, "<div id=\"infoboxes\">\n");
			infobox(b, s, r->infoboxes[0]);
			buf_puts(b, "</div>\n");
		}
		engine_timing(b, s);
		buf_puts(b, "</div>\n");
	}

	buf_puts(b, "<div id=\"urls\" role=\"main\">\n");
	if (images)
		engine_timing(b, s);
	for (i = 0; i < r->nmain; i++) {
		if (!strcmp(r->main[i]->template, "images"))
			image_result(b, s, r->main[i]);
		else
			main_result(b, s, r->main[i]);
	}
	if (!r->nmain && !r->nanswers)
		buf_html(b,
		    "<div class=\"dialog-error-block\" role=\"alert\"><p><strong>%r</strong></p><p>%r</p></div>\n",
		    s->pageno == 1 ? "Sorry!" : "",
		    s->pageno == 1 ? "No results were found." : "There are no more results.");
	buf_puts(b, "</div>\n");
	if (r->paging)
		pagination(b, s);
	buf_puts(b, "</div>\n");
	if (images)
		image_detail_template(b);
	buf_puts(b, "</body>\n</html>\n");
}

void
render_opensearch(struct buf *b, const char *proto, const char *host)
{
	buf_html(b,
	    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	    "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
	    "<ShortName>privy</ShortName>\n"
	    "<Description>privy metasearch</Description>\n"
	    "<InputEncoding>UTF-8</InputEncoding>\n"
	    "<Url type=\"text/html\" method=\"get\" template=\"%s://%s/search?q={searchTerms}\"/>\n"
	    "<Url type=\"application/x-suggestions+json\" method=\"get\" template=\"%s://%s/autocompleter?q={searchTerms}\"/>\n"
	    "</OpenSearchDescription>\n",
	    proto, host, proto, host);
}
