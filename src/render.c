#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "autocomplete.h"
#include "botdetect.h"
#include "engine.h"
#include "favicon.h"
#include "icons.h"
#include "proxy.h"
#include "render.h"
#include "stats.h"
#include "version.h"

#define LOGO \
	"<svg class=\"logo\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"3.2 2 21.6 21.6\" aria-hidden=\"true\"><defs>" \
	"<linearGradient id=\"logo-rainbow\" gradientUnits=\"userSpaceOnUse\" x1=\"3.2\" y1=\"2\" x2=\"29.2\" y2=\"11.5\">" \
	"<stop offset=\"0\"/><stop offset=\".22\"/><stop offset=\".22\"/><stop offset=\".34\"/><stop offset=\".34\"/><stop offset=\".46\"/><stop offset=\".46\"/><stop offset=\".58\"/><stop offset=\".58\"/><stop offset=\".7\"/><stop offset=\".7\"/><stop offset=\".82\"/><stop offset=\".82\"/><stop offset=\"1\"/></linearGradient></defs>" \
	"<path fill=\"url(#logo-rainbow)\" fill-rule=\"evenodd\" stroke=\"url(#logo-rainbow)\" stroke-width=\"1.4\" " \
	"stroke-linejoin=\"round\" d=\"M6.02 7.81L5.7 2.9l4.53 2.02A7.3 7.3 0 0 1 13.77 4.92L18.3 2.9l-.32 4.91A7.3 7.3 0 1 1 " \
	"6.02 7.81zM17.5 12a5.5 5.5 0 1 0-11 0 5.5 5.5 0 0 0 11 0z\"/>" \
	"<path d=\"M17.2 17.2L21.8 21.8\" stroke=\"url(#logo-rainbow)\" stroke-width=\"3.6\" stroke-linecap=\"round\" " \
	"fill=\"none\"/></svg>"

#define SOURCE_URL "https://github.com/vojkovic/priv"

#define YES "<span class=\"yes\">" ICON_CHECK "</span>"
#define NO "<span class=\"no\">" ICON_MINUS "</span>"

/* images the proxy already prefetched go inline, the rest are fetched through it */
#define INLINE_MAX (12 << 10)
#define INFOBOX_INLINE_MAX (64 << 10)
#define FAVICON_INLINE_MAX (3 << 10)
#define FAVICON_INLINE_SITES 16

static const char *safe_levels[] = {"None", "Moderate", "Strict"};
static struct arena scratch;

/* each site's favicon is inlined once, in a sprite at the end of the page, and referenced from there */
struct favicon {
	const char *authority, *data;
	uint32_t id;
};

static struct favicon favicons[128];
static size_t nfavicons, ninlined;

/* helpers */

static const char *
html_str(struct arena *a, const char *s)
{
	struct buf b = {0};
	const char *r;

	html_escape(&b, s);
	r = astrdup(a, buf_str(&b));
	buf_free(&b);
	return r;
}

/* writes {0} and {1} in a translated message as given, already escaped, and escapes the rest */
static void
put_tr(struct buf *b, const char *msgid, const char *arg0, const char *arg1)
{
	const char *t = tr(msgid), *p;

	for (p = t; *p;) {
		if (p[0] == '{' && (p[1] == '0' || p[1] == '1') && p[2] == '}') {
			buf_puts(b, p[1] == '0' ? arg0 : arg1);
			p += 3;
			continue;
		}
		html_escape(b, (char[]){*p, 0});
		p++;
	}
}

static void
option(struct buf *b, const char *value, const char *label, int selected)
{
	buf_html(b, "<option value=\"%s\"%r>%s</option>", value, selected ? " selected" : "", label);
}

/* page shell */

static void
doc_start(struct buf *b, const char *title, const char *body_class, const struct prefs *p)
{
	const struct ui_lang *ui = p->ui;

	i18n_use(ui);
	buf_html(b,
	    "<!doctype html>\n"
	    "<html lang=\"%s\" data-theme=\"%s\"%r>\n"
	    "<head>\n"
	    "<meta charset=\"utf-8\">\n"
	    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
	    "<meta name=\"referrer\" content=\"no-referrer\">\n"
	    "<title>%s</title>\n"
	    "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">\n"
	    "<link rel=\"stylesheet\" href=\"/style.css\">\n"
	    "<link rel=\"search\" type=\"application/opensearchdescription+xml\" title=\"%s\" href=\"/opensearch.xml\">\n"
	    "<script src=\"/priv.js\" defer></script>\n",
	    ui->tag, p->theme, ui->rtl ? " dir=\"rtl\"" : "", title, instance_name);
	if (limiter && limiter_link_token)
		buf_html(b, "<link rel=\"stylesheet\" href=\"/client%s.css\">\n", botdetect_token());
	buf_html(b,
	    "</head>\n"
	    "<body class=\"%s\">\n",
	    body_class);
}

static void
nav_links(struct buf *b)
{
	buf_html(b,
	    "<nav id=\"links\">"
	    "<a href=\"/about\" aria-label=\"%s\" title=\"%s\">" ICON_INFO "</a>"
	    "<a href=\"/preferences\" aria-label=\"%s\" title=\"%s\">" ICON_SETTINGS "</a>"
	    "</nav>\n",
	    tr("About"), tr("About"), tr("Preferences"), tr("Preferences"));
}

/* the shell of about and stats: nav icons, a back arrow and a heading */
static void
page_start(struct buf *b, const struct prefs *p, const char *title, const char *heading)
{
	i18n_use(p->ui);
	doc_start(b, aprintf(&scratch, "%s - %s", tr(title), instance_name), "page_endpoint", p);
	nav_links(b);
	buf_html(b,
	    "<main class=\"page\">\n"
	    "<h1><a href=\"/\" aria-label=\"%s\">" ICON_BACK "</a>%s</h1>\n",
	    tr("Back"), heading);
	arena_free(&scratch);
}

/* search header */

static void
search_box(struct buf *b, const char *query, const char *category, int autofocus)
{
	buf_html(b,
	    "<div id=\"search_view\">"
	    "<div class=\"search_box\">"
	    "<button id=\"search_back\" type=\"button\" aria-label=\"%s\">" ICON_BACK "</button>"
	    "<input id=\"q\" type=\"search\" name=\"q\" tabindex=\"1\" value=\"%s\" placeholder=\"%s\" aria-label=\"%s\" "
	    "autocomplete=\"off\" autocapitalize=\"none\" spellcheck=\"false\" dir=\"auto\"%r>"
	    "<button id=\"send_search\" type=\"submit\" name=\"categories\" value=\"%s\" aria-label=\"%s\">" ICON_SEARCH
	    "</button>"
	    "<div class=\"autocomplete\"><ul></ul></div>"
	    "</div>"
	    "</div>\n",
	    tr("Back"), query, tr("Search for..."), tr("Search"), autofocus ? " data-autofocus" : "", category,
	    tr("Search"));
}

static void
category_tabs(struct buf *b, const char *current)
{
	size_t i;

	buf_puts(b, "<div id=\"categories\"><div id=\"categories_container\">");
	for (i = 0; i < ncategories; i++)
		buf_html(b,
		    "<button type=\"submit\" name=\"categories\" value=\"%s\" class=\"category_button%r\" aria-label=\"%s\">"
		    "%r<span class=\"category_name\">%s</span>"
		    "</button>",
		    categories[i].name, strcmp(categories[i].name, current) ? "" : " selected", tr(categories[i].label),
		    categories[i].icon, tr(categories[i].label));
	buf_puts(b, "</div></div>\n");
}

static void
language_options(struct buf *b, const char *selected, const char *detected)
{
	size_t i;

	buf_html(b, "<option value=\"auto\"%r>%s (%s)</option>", !strcmp(selected, "auto") ? " selected" : "",
	    tr("Auto"), detected);
	buf_html(b, "<option value=\"all\"%r>%s</option>", !strcmp(selected, "all") ? " selected" : "",
	    tr("Default language"));
	for (i = 1; i < nlocales; i++)
		option(b, locales[i].tag, locales[i].label, !strcmp(selected, locales[i].tag));
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
	size_t i;

	buf_html(b,
	    "<div class=\"search_filters\">\n"
	    "<select name=\"language\" form=\"search\" aria-label=\"%s\">",
	    tr("Search language"));
	language_options(b, f->language, f->browser->label);
	buf_html(b,
	    "</select>\n"
	    "<select name=\"time_range\" form=\"search\" aria-label=\"%s\">",
	    tr("Time range"));
	for (i = 0; i < LEN(ranges); i++)
		option(b, ranges[i][0], tr(ranges[i][1]), f->time_range ? !strcmp(f->time_range, ranges[i][0]) : !i);
	buf_html(b,
	    "</select>\n"
	    "<select name=\"safesearch\" form=\"search\" aria-label=\"%s\">",
	    tr("SafeSearch"));
	for (i = 3; i-- > 0;)
		option(b, (const char *[]){"0", "1", "2"}[i],
		    aprintf(&scratch, "%s: %s", tr("SafeSearch"), tr(safe_levels[i])), f->safesearch == (int)i);
	buf_puts(b,
	    "</select>\n"
	    "</div>\n");
	arena_free(&scratch);
}

/* the filters a link has to carry, those that differ from the user's defaults */
static void
filter_params(struct buf *b, const struct filters *f)
{
	if (strcmp(f->category, "general"))
		buf_html(b, "&amp;categories=%u", f->category);
	if (strcmp(f->language, f->prefs.language ? f->prefs.language : "auto"))
		buf_html(b, "&amp;language=%u", f->language);
	if (f->time_range)
		buf_html(b, "&amp;time_range=%u", f->time_range);
	if (f->safesearch != f->prefs.safesearch)
		buf_html(b, "&amp;safesearch=%d", f->safesearch);
}

static void
footer(struct buf *b)
{
	const char *plus = strchr(VERSION, '+');

	buf_puts(b, "<footer id=\"footer\">");
	if (plus && !strstr(VERSION, "-dirty"))
		buf_html(b, "<a href=\"" SOURCE_URL "/commit/%s\">%s</a>", plus + 1, VERSION);
	else if (*VERSION)
		buf_html(b, "<span>%s</span>", VERSION);
	buf_html(b,
	    "<nav>"
	    "<a href=\"" SOURCE_URL "\">%s</a>"
	    "<a href=\"" SOURCE_URL "/issues\">%s</a>"
	    "<a href=\"/stats\">%s</a>"
	    "</nav>"
	    "</footer>\n",
	    tr("Source code"), tr("Issue tracker"), tr("Engine stats"));
}

void
render_home(struct buf *b, const struct prefs *p)
{
	size_t n = 0;

	doc_start(b, instance_name, "index_endpoint", p);
	nav_links(b);
	buf_html(b,
	    "<main id=\"main_index\">\n"
	    "<h1 class=\"title\">%s</h1>\n",
	    instance_name);
	while (motd && motd[n])
		n++;
	if (n)
		buf_html(b, "<p class=\"motd\">%s</p>\n", motd[rnd((uint32_t)n)]);
	buf_puts(b, "<form id=\"search\" action=\"/search\" method=\"get\" role=\"search\">\n");
	search_box(b, "", "general", 1);
	buf_puts(b, "</form>\n");
	footer(b);
	buf_puts(b,
	    "</main>\n"
	    "</body>\n"
	    "</html>\n");
}

/* whether a query reads as arithmetic, which loads the calculator; calc.js does the real parsing */
static int
math_query(const char *q)
{
	static const char *const fillers[] = {"what", "is", "whats", "calculate", "calc", "compute", "solve", "equals",
	    "to", "the", "by"};
	static const char *const constants[] = {"pi", "e", "tau", "phi", "ans"};
	static const char *const operations[] = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh",
	    "tanh", "asinh", "acosh", "atanh", "arcsin", "arccos", "arctan", "sqrt", "cbrt", "cuberoot", "root", "abs",
	    "sign", "sgn", "floor", "ceil", "ceiling", "trunc", "round", "exp", "ln", "log", "log2", "log10", "lg",
	    "pow", "hypot", "min", "max", "mod", "modulo", "gcd", "lcm", "comb", "perm", "ncr", "npr", "factorial",
	    "fact", "radians", "degrees", "deg", "rad", "of", "percent", "cent", "per", "plus", "minus", "times",
	    "multiplied", "divided", "over", "squared", "cubed", "power", "raised"};
	/* UTF-8 for the times, divide, minus, dot, power, degree, pi and root signs */
	static const char *const symbols[] = {"\xc3\x97", "\xc3\xb7", "\xe2\x88\x92", "\xe2\x80\x93", "\xe2\x80\x94",
	    "\xc2\xb7", "\xe2\x8b\x85", "\xe2\x88\x97", "\xc2\xb2", "\xc2\xb3", "\xc2\xb0", "\xcf\x80", "\xe2\x88\x9a",
	    "\xe2\x88\x9b"};
	size_t n = strlen(q), i, k, len;
	int operators = 0, operands = 0, hyphens = 0, dashed = 1;
	char word[16];

	if (!n || n > 200)
		return 0;
	for (i = 0; i < n;) {
		if (IS_DIGIT(q[i])) {
			operands = 1;
			i++;
		} else if (IS_ALPHA(q[i])) {
			for (len = 0; IS_ALPHA(q[i + len]) || IS_DIGIT(q[i + len]); len++)
				if (len >= sizeof(word) - 1)
					return 0;
			memcpy(word, q + i, len);
			word[len] = 0;
			i += len;
			dashed = 0;
			if (i > len && IS_DIGIT(q[i - len - 1]) && (word[0] | 0x20) == 'e' &&
			    strspn(word + 1, "0123456789") == len - 1)
				continue;
			for (k = 0; k < LEN(operations) && strcasecmp(word, operations[k]); k++)
				;
			if (k < LEN(operations)) {
				operators = 1;
				continue;
			}
			for (k = 0; k < LEN(constants) && strcasecmp(word, constants[k]); k++)
				;
			if (k < LEN(constants)) {
				operands = 1;
				continue;
			}
			for (k = 0; k < LEN(fillers) && strcasecmp(word, fillers[k]); k++)
				;
			if (k == LEN(fillers))
				return 0;
		} else if (strchr(" \t.,'=()", q[i])) {
			dashed = 0;
			i++;
		} else if (strchr("+-*/^%!", q[i])) {
			operators = 1;
			hyphens += q[i] == '-';
			dashed &= q[i] == '-';
			i++;
		} else {
			for (k = 0; k < LEN(symbols) && strncmp(q + i, symbols[k], strlen(symbols[k])); k++)
				;
			if (k == LEN(symbols))
				return 0;
			operators = 1;
			operands |= k == 11;
			dashed = 0;
			i += strlen(symbols[k]);
		}
	}
	/* 555-1234-9876 and 2024-10-25 are phone numbers and dates */
	return operators && operands && !(dashed && hyphens >= 2);
}

/* whether a query could be a unit conversion such as "5 km to miles"; calc.js checks the units */
static int
unit_query(const char *q)
{
	static const char *const connectors[] = {"to", "in", "into", "as", "equals", "->", "=>", "="};
	size_t n = strlen(q), i = 0, len, k, words = 0, joint = 0;
	int digits = 0, found = 0, asking;
	char word[8];

	if (!n || n > 120)
		return 0;
	asking = !strncasecmp(q, "convert ", 8) || !strncasecmp(q, "how many ", 9) || !strncasecmp(q, "how much ", 9) ||
	    !strncasecmp(q, "what is ", 8);
	while (i < n) {
		while (q[i] == ' ' || q[i] == '\t')
			i++;
		if (i >= n)
			break;
		len = strcspn(q + i, " \t");
		for (k = 0; k < len; k++)
			digits |= IS_DIGIT(q[i + k]);
		if (len < sizeof(word) && !found && words) {
			memcpy(word, q + i, len);
			word[len] = 0;
			for (k = 0; k < LEN(connectors); k++)
				if (!strcasecmp(word, connectors[k])) {
					found = 1;
					joint = words;
				}
		}
		words++;
		i += len;
	}
	return found && joint < words - 1 && words >= 3 && words <= 10 && (digits || asking || words <= 4);
}

/* whether a query is a number and a unit, such as "38 oz"; calc.js checks the unit */
static int
quantity_query(const char *q)
{
	size_t n = strlen(q), i, words = 1;

	if (!n || n > 40)
		return 0;
	for (i = 0; i < n && (IS_DIGIT(q[i]) || strchr(".,/-", q[i])); i++)
		;
	if (i == n || !IS_DIGIT(q[0]) || (q[i] != ' ' && !IS_ALPHA(q[i]) && (unsigned char)q[i] < 0x80))
		return 0;
	for (; i < n; i++)
		words += q[i] == ' ';
	return words <= 3 && (IS_ALPHA(q[n - 1]) || (unsigned char)q[n - 1] >= 0x80);
}

/* whether a query asks for a colour, such as "color picker", "#f0f0f0" or "rgb 255 255 255"; color.js reads it */
static int
color_query(const char *q)
{
	static const char *const phrases[] = {"color picker", "colour picker", "colorpicker", "colourpicker",
	    "hex to rgb", "rgb to hex", "hex to hsl", "hsl to hex", "rgb to hsl", "hsl to rgb", "rgb to hsv",
	    "hex to hsv", "rgb to cmyk", "hex to cmyk", "html color", "color converter", "colour converter",
	    "color code", "colour code"};
	static const char *const notations[] = {"rgb", "rgba", "hsl", "hsla", "hsv", "hsb", "cmyk"};
	size_t n = strlen(q), i, len;

	if (!n || n > 80)
		return 0;
	while (*q == ' ')
		q++;
	for (i = 0; i < LEN(phrases); i++)
		if (strcasestr(q, phrases[i]))
			return 1;
	if (*q == '#') {
		len = strspn(q + 1, "0123456789abcdefABCDEF");
		return (len == 3 || len == 4 || len == 6 || len == 8) && (!q[len + 1] || q[len + 1] == ' ');
	}
	if (!strncasecmp(q, "color ", 6) || !strncasecmp(q, "colour ", 7) || !strncasecmp(q, "hex ", 4)) {
		q += strcspn(q, " ") + 1;
		len = strspn(q, "0123456789abcdefABCDEF");
		return (len == 3 || len == 4 || len == 6 || len == 8) && !q[len];
	}
	for (i = 0; i < LEN(notations); i++) {
		len = strlen(notations[i]);
		if (!strncasecmp(q, notations[i], len)) {
			q += len;
			q += strspn(q, " (");
			return IS_DIGIT(*q) || ((*q == '-' || *q == '.') && IS_DIGIT(q[1]));
		}
	}
	return 0;
}

void
render_head(struct buf *b, struct search *s)
{
	struct buf page = {0};
	int seconds = (int)((s->filters.timeout > 0 ? s->filters.timeout : search_timeout) + 0.99);

	i18n_use(s->filters.prefs.ui);
	put_tr(&page, "Page {0}", aprintf(&s->a, "%d", s->pageno), "");
	doc_start(b,
	    s->pageno > 1 ? aprintf(&s->a, "%s - %s - %s", s->raw, buf_str(&page), instance_name) :
	                    aprintf(&s->a, "%s - %s", s->raw, instance_name),
	    "results_endpoint", &s->filters.prefs);
	buf_free(&page);
	buf_html(b,
	    "<header id=\"search_header\">\n"
	    "<a id=\"search_logo\" href=\"/\" aria-label=\"%s\">" LOGO "</a>\n"
	    "<form id=\"search\" action=\"/search\" method=\"get\" role=\"search\">\n",
	    instance_name);
	search_box(b, s->raw, s->filters.category, 0);
	category_tabs(b, s->filters.category);
	buf_puts(b, "</form>\n");
	nav_links(b);
	buf_html(b,
	    "</header>\n"
	    "<div class=\"loading t%d\" role=\"progressbar\" aria-label=\"%s\"></div>\n",
	    MIN(MAX(seconds, 1), 10), tr("Searching"));
	search_filters(b, &s->filters);
	buf_html(b, "<div id=\"results\" class=\"category-%s\">\n", s->filters.category);
	if (s->pageno == 1 && !strcmp(s->filters.category, "general")) {
		if (color_query(s->raw))
			buf_puts(b, "<script src=\"/color.js\"></script>\n");
		else if (math_query(s->raw) || unit_query(s->raw) || quantity_query(s->raw))
			buf_puts(b, "<script src=\"/calc.js\"></script>\n");
	}
}

/* results */

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
			buf_html(b, "<div><span>%s:</span> %s</div>\n", tr(at->label), at->value);
	}
	if (m->pdf_url && *m->pdf_url)
		buf_html(b, "<div class=\"links\"><a href=\"%s\" rel=\"noreferrer\">PDF</a></div>\n", m->pdf_url);
	buf_puts(b, "</div>\n");
}

static const char *
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

static void
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

static void
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

/* images */

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

/* infobox and answers */

static const char *
url_authority(struct arena *a, const char *url)
{
	struct url u;

	url_parse(a, url, &u);
	return u.netloc;
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
	char *img = image_url(&s->a, ib->img_src);
	int chips = 0;

	for (u = ib->urls; u && !source; u = u->next)
		if (u->title && !strcmp(u->title, "Wikipedia"))
			source = u;

	buf_puts(b, "<aside class=\"infobox\">\n");
	if (img)
		buf_html(b, "<img class=\"infobox_image\" src=\"%s\" alt=\"\">\n",
		    image_src(s, ib->img_src, img, INFOBOX_INLINE_MAX));
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
		buf_html(b,
		    "<div class=\"also\">"
		    "<span class=\"also_label\">%s</span>"
		    "<ul>\n",
		    tr("Also refers to"));
		for (u = ib->also; u; u = u->next) {
			buf_html(b, "<li><a href=\"/search?q=%u", u->title);
			filter_params(b, &s->filters);
			buf_html(b, "\">%s</a>", u->title);
			if (u->entity)
				buf_html(b, "<span>%s</span>", u->entity);
			buf_puts(b, "</li>\n");
		}
		buf_puts(b,
		    "</ul>"
		    "</div>\n");
	}

	for (u = ib->urls; u; u = u->next) {
		if (u == source)
			continue;
		if (!chips++)
			buf_puts(b, "<div class=\"links\">");
		buf_html(b, "<a href=\"%s\" rel=\"noreferrer\">", u->url);
		favicon(b, s, url_authority(&s->a, u->url));
		buf_html(b, "%s</a>", u->title && *u->title ? u->title : u->url);
	}
	if (chips)
		buf_puts(b, "</div>\n");

	if (source)
		buf_html(b, "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">Wikipedia" ICON_ARROW_NE "</a>\n",
		    source->url);
	buf_puts(b, "</aside>\n");
}

/* answers that only repeat the infobox's links are dropped */
static void
answers(struct buf *b, struct search *s)
{
	struct results *r = &s->res;
	size_t i, n;

	for (i = n = 0; i < r->nanswers; i++)
		if (!in_infobox(r, r->answers[i]->url))
			r->answers[n++] = r->answers[i];
	r->nanswers = n;
	if (!r->nanswers)
		return;
	buf_puts(b, "<div id=\"answers\">\n");
	for (i = 0; i < r->nanswers; i++) {
		buf_html(b, "<div class=\"answer\"><p>%s</p>", r->answers[i]->answer);
		if (r->answers[i]->url)
			buf_html(b, "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">%s" ICON_ARROW_NE "</a>",
			    r->answers[i]->url, source_name(&s->a, r->answers[i]->url));
		buf_puts(b, "</div>\n");
	}
	buf_puts(b, "</div>\n");
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

/* engine timings */

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
	double max, limit = s->filters.timeout > 0 ? s->filters.timeout : search_timeout;
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

/* pagination */

static void
token_params(struct buf *b, struct search *s)
{
	struct buf t = {0};
	size_t i;

	for (i = 0; i < s->njobs; i++)
		if (s->jobs[i].next_token)
			form_add(&t, s->jobs[i].e->name, s->jobs[i].next_token);
	if (t.len)
		buf_html(b, "&amp;pt=%u&amp;ph=%s", buf_str(&t), proxy_hmac(&s->a, buf_str(&t)));
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
	answers(b, s);

	/* images use the full width, so their timings take a grid slot instead of the sidebar */
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

/* preferences */

static void
pref_select(struct buf *b, const char *name, const char *label, const char *help)
{
	buf_html(b,
	    "<div class=\"pref\">"
	    "<label for=\"%s\"><strong>%s</strong><span>%s</span></label>"
	    "<select id=\"%s\" name=\"%s\">",
	    name, tr(label), tr(help), name, name);
}

static void
engine_table(struct buf *b, const struct prefs *p, size_t cat)
{
	const char *name = categories[cat].name;
	const struct engine *e;
	size_t j, total = 0;

	for (j = 0; j < nengines; j++)
		total += engine_in(engines[j], name);
	buf_html(b,
	    "<details class=\"engine_group\"%r>"
	    "<summary>%r<span>%s</span><span class=\"count\">%z</span>" ICON_EXPAND "</summary>"
	    "<div class=\"table\">"
	    "<table>\n"
	    "<thead><tr><th>%s</th><th>%s</th><th>!bang</th><th>%s</th><th>%s</th><th>%s</th></tr></thead>"
	    "<tbody>\n",
	    cat ? "" : " open", categories[cat].icon, tr(categories[cat].label), total, tr("Allow"), tr("Engine name"),
	    tr("Pages"), tr("SafeSearch"), tr("Time range"));
	for (j = 0; j < nengines; j++) {
		e = engines[j];
		if (!engine_in(e, name))
			continue;
		buf_html(b,
		    "<tr>"
		    "<td><input type=\"checkbox\" class=\"switch\" id=\"%s.%s\" name=\"on\" value=\"%s.%s\"%r></td>"
		    "<td><label for=\"%s.%s\">%s</label></td>"
		    "<td><code>!%s</code></td>"
		    "<td>%r</td><td>%r</td><td>%r</td>"
		    "</tr>\n",
		    name, e->shortcut, name, e->shortcut, p->off[cat] & ENGINE_BIT(e) ? "" : " checked", name,
		    e->shortcut, e->name, e->shortcut, e->paging ? YES : NO, e->safesearch ? YES : NO,
		    e->time_range ? YES : NO);
	}
	buf_puts(b,
	    "</tbody>"
	    "</table>"
	    "</div>"
	    "</details>\n");
}

void
render_preferences(struct buf *b, const struct prefs *p, const struct locale *browser)
{
	size_t i;

	doc_start(b, aprintf(&scratch, "%s - %s", tr("Preferences"), instance_name), "preferences_endpoint", p);
	nav_links(b);
	buf_html(b,
	    "<main id=\"preferences\">\n"
	    "<h1><a href=\"/\" aria-label=\"%s\">" ICON_BACK "</a>%s</h1>\n"
	    "<form action=\"/preferences\" method=\"get\">\n"
	    "<input type=\"hidden\" name=\"save\" value=\"1\">\n",
	    tr("Back"), tr("Preferences"));

	pref_select(b, "ui", "Interface language", "Change the language of the layout");
	buf_html(b, "<option value=\"\"%r>%s (%s)</option>", *p->ui_code ? "" : " selected", tr("Auto"),
	    p->ui_auto->name);
	for (i = 0; i < nui_langs; i++)
		option(b, ui_langs[i].code, ui_langs[i].name, !strcmp(p->ui_code, ui_langs[i].code));
	buf_puts(b, "</select></div>\n");

	pref_select(b, "theme", "Theme", "Change the look of the layout");
	for (i = 0; i < nthemes; i++)
		option(b, themes[i].name, themes[i].label, !strcmp(p->theme, themes[i].name));
	buf_puts(b, "</select></div>\n");

	pref_select(b, "favicon", "Favicon Resolver", "Display favicons near search results");
	for (i = 0; i < nfavicon_resolvers; i++)
		option(b, favicon_resolvers[i].name, favicon_resolvers[i].label,
		    !strcmp(p->favicon, favicon_resolvers[i].name));
	buf_puts(b, "</select></div>\n");

	pref_select(b, "language", "Search language", "What language do you prefer for search?");
	language_options(b, p->language ? p->language : "auto", browser->label);
	buf_puts(b, "</select></div>\n");

	pref_select(b, "safesearch", "SafeSearch", "Filter content");
	for (i = 0; i < 3; i++)
		option(b, (const char *[]){"0", "1", "2"}[i], tr(safe_levels[i]), p->safesearch == (int)i);
	buf_puts(b, "</select></div>\n");

	pref_select(b, "autocomplete", "Autocomplete", "Show possible queries as you type");
	for (i = 0; i < nautocompleters; i++)
		option(b, autocompleters[i].name, tr(autocompleters[i].label),
		    !strcmp(p->autocomplete, autocompleters[i].name));
	buf_puts(b, "</select></div>\n");

	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++)
		engine_table(b, p, i);

	buf_html(b,
	    "<div class=\"buttons\">"
	    "<a href=\"/preferences?reset=1\">%s</a>"
	    "<button type=\"submit\">%s</button>"
	    "</div>\n"
	    "</form>\n"
	    "</main>\n"
	    "</body>\n"
	    "</html>\n",
	    tr("Reset defaults"), tr("Save"));
	arena_free(&scratch);
}

/* simple pages */

void
render_not_found(struct buf *b, const struct prefs *p)
{
	doc_start(b, aprintf(&scratch, "Page not found - %s", instance_name), "not_found_endpoint", p);
	arena_free(&scratch);
	nav_links(b);
	buf_puts(b,
	    "<main id=\"not_found\">\n"
	    "<h1>Page not found</h1>\n"
	    "<p>There's nothing here. <a href=\"/\">Go to the search page</a>.</p>\n"
	    "</main>\n"
	    "</body>\n"
	    "</html>\n");
}

void
render_about(struct buf *b, const struct prefs *p, const char *origin)
{
	page_start(b, p, "About", aprintf(&scratch, "About %s", instance_name));
	buf_html(b,
	    "<p>%s is a <a href=\"https://en.wikipedia.org/wiki/Metasearch_engine\">metasearch engine</a> inspired by "
	    "<a href=\"https://github.com/searxng/searxng\">SearXNG</a> which aggregates the results from other search "
	    "engines whilst not storing any information on users.</p>\n"
	    "<p>It runs <a href=\"" SOURCE_URL "\">priv</a> which is open source software you can host "
	    "yourself. If you find it useful, you can <a href=\"https://ko-fi.com/privau\">support its development</a>.</p>\n"

	    "<h2>Add it to your browser</h2>\n"
	    "<p>Firefox: right click the address bar while on %s and choose Add \"%s\". Then pick it under Settings &gt; "
	    "Search &gt; Default Search Engine.</p>\n"
	    "<p>Chrome: do a search and then open Settings &gt; Search engine &gt; Manage search engines and site search. "
	    "Under Inactive shortcuts click Activate, then Make default from the \xe2\x8b\xae menu.</p>\n"
	    "<p>If your browser doesn't support <a href=\"https://en.wikipedia.org/wiki/OpenSearch_(specification)\">"
	    "OpenSearch</a> you can use these URLs to add it manually:</p>\n"
	    "<dl class=\"stacked\">\n"
	    "<dt>Search</dt><dd><code>%s/search?q=%%s</code></dd>\n"
	    "<dt>Suggestions</dt><dd><code>%s/autocompleter?q=%%s</code></dd>\n"
	    "</dl>\n"

	    "<h2>Search syntax</h2>\n"
	    "<dl class=\"stacked\">\n"
	    "<dt><code>!g cats</code></dt><dd>Search using the Google engine.</dd>\n"
	    "<dt><code>!!g cats</code></dt><dd>Redirect to Google's own results.</dd>\n"
	    "<dt><code>:de cats</code></dt><dd>Search with German language.</dd>\n"
	    "<dt><code>&lt;3 cats</code></dt><dd>Set timeout to 3 seconds.</dd>\n"
	    "</dl>\n"
	    "</main>\n"
	    "</body>\n"
	    "</html>\n",
	    instance_name, instance_name, instance_name, origin, origin);
	arena_free(&scratch);
}

/* stats */

struct stats_row {
	const struct engine *e;
	int reliability, suspended;
	double typical;
	const char *error;
};

static int
float_cmp(const void *x, const void *y)
{
	float a = *(const float *)x, b = *(const float *)y;

	return a < b ? -1 : a > b;
}

static int
stats_cmp(const void *x, const void *y)
{
	return strcmp(((const struct stats_row *)x)->e->name, ((const struct stats_row *)y)->e->name);
}

/* reliability is the share of recent searches the engine answered, typical its median time for those */
static void
stats_row(struct stats_row *row, const struct engine *e)
{
	const struct engine_stats *st = stats_of(e);
	float times[STATS_WINDOW] = {0};
	size_t k, n = 0;

	row->e = e;
	row->suspended = e->suspend_end > now_ms();
	row->reliability = -1;
	row->typical = 0;
	row->error = NULL;
	if (!st)
		return;
	for (k = 0; k < st->nrecent; k++)
		if (st->recent[k].outcome == OUTCOME_OK || st->recent[k].outcome == OUTCOME_EMPTY)
			times[n++] = st->recent[k].seconds;
	qsort(times, n, sizeof(*times), float_cmp);
	if (st->nrecent)
		row->reliability = (int)(n * 100 / st->nrecent);
	row->typical = n ? times[n / 2] : 0;
	row->error = *st->error ? st->error : NULL;
}

void
render_stats(struct buf *b, const struct prefs *p)
{
	struct stats_row *rows, *r;
	const char *cls;
	size_t i;

	page_start(b, p, "Engine stats", "Engine stats");
	rows = amalloc(&scratch, (nengines + 1) * sizeof(*rows));
	for (i = 0; i < nengines; i++)
		stats_row(&rows[i], engines[i]);
	qsort(rows, nengines, sizeof(*rows), stats_cmp);

	buf_puts(b,
	    "<div class=\"stats\">"
	    "<table>\n"
	    "<thead><tr><th>Engine</th><th>Reliability</th><th>Response time</th><th>Last error</th></tr></thead>"
	    "<tbody>\n");
	for (i = 0; i < nengines; i++) {
		r = &rows[i];
		cls = r->suspended || (r->reliability >= 0 && r->reliability < 50) ? "bad" :
		    r->reliability >= 0 && r->reliability < 85                     ? "warn" :
		                                                                     "";
		buf_html(b,
		    "<tr>"
		    "<td><img src=\"%s\" alt=\"\" loading=\"lazy\">%s</td>"
		    "<td class=\"%r\">",
		    favicon_url(&scratch, r->e->website, p->favicon), r->e->name, cls);
		if (r->suspended)
			buf_puts(b, "suspended");
		else if (r->reliability >= 0)
			buf_printf(b, "%d%%", r->reliability);
		buf_puts(b, "</td><td>");
		if (r->typical > 0)
			buf_printf(b, "%.2fs", r->typical);
		buf_html(b,
		    "</td>"
		    "<td>%s</td>"
		    "</tr>\n",
		    r->suspended ? r->e->suspend_reason :
		        r->error ? r->error :
		                   "");
	}
	buf_puts(b,
	    "</tbody>"
	    "</table>"
	    "</div>\n"
	    "</main>\n"
	    "</body>\n"
	    "</html>\n");
	arena_free(&scratch);
}

/* opensearch */

void
render_opensearch(struct buf *b, const char *proto, const char *host)
{
	buf_html(b,
	    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	    "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
	    "<ShortName>%s</ShortName>\n"
	    "<Description>%s metasearch</Description>\n"
	    "<InputEncoding>UTF-8</InputEncoding>\n"
	    "<Url type=\"text/html\" method=\"get\" template=\"%s://%s/search?q={searchTerms}\"/>\n"
	    "<Url type=\"application/x-suggestions+json\" method=\"get\" template=\"%s://%s/autocompleter?q={searchTerms}\"/>\n"
	    "</OpenSearchDescription>\n",
	    instance_name, instance_name, proto, host, proto, host);
}
