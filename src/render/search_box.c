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

void
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

void
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

void
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

void
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
void
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
