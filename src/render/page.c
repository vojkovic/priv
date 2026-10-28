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

//
// The page shell and the smaller pages
//
// everything that is not the results page: the document start, navigation,
// footer, and the pages for not found, about and the opensearch file. the
// helpers at the top are shared by all of render/.
//

const char *safe_levels[] = {"None", "Moderate", "Strict"};
struct arena scratch;

const char *
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
void
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

void
option(struct buf *b, const char *value, const char *label, int selected)
{
	buf_html(b, "<option value=\"%s\"%r>%s</option>", value, selected ? " selected" : "", label);
}

void
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
		buf_html(b,
		    "<link rel=\"stylesheet\" href=\"/client%s.css\">\n"
		    "<template><link rel=\"stylesheet\" href=\"/client%s.css\"></template>\n",
		    botdetect_token(), botdetect_decoy());
	buf_html(b,
	    "</head>\n"
	    "<body class=\"%s\">\n",
	    body_class);
}

void
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
void
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

void
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
