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
	int opts[16], dflt = (int)(categories[cat].timeout * 10 + 0.5), done = 0;
	int tenths = p->timeout[cat] ? p->timeout[cat] : dflt;
	size_t j, k, n = 0, total = 0;

	for (j = 0; j < nengines; j++)
		total += engine_in(engines[j], name);
	buf_html(b,
	    "<details class=\"engine_group\"%r>"
	    "<summary>%r<span>%s</span><span class=\"count\">%z</span>" ICON_EXPAND "</summary>"
	    "<div class=\"table\">",
	    cat ? "" : " open", categories[cat].icon, tr(categories[cat].label), total);
	pref_select(b, aprintf(&scratch, "timeout_%s", name), "Timeout", "How long to wait for engines");
	/* the instance's default is offered even when it is not one of the usual choices */
	for (k = 0; k < ntimeout_choices; k++) {
		if (!done && timeout_choices[k] >= dflt) {
			opts[n++] = dflt;
			done = 1;
			if (timeout_choices[k] == dflt)
				continue;
		}
		opts[n++] = timeout_choices[k];
	}
	if (!done)
		opts[n++] = dflt;
	for (k = 0; k < n; k++)
		buf_html(b, "<option value=\"%d\"%r>%d.%d s</option>", opts[k], opts[k] == tenths ? " selected" : "",
		    opts[k] / 10, opts[k] % 10);
	buf_puts(b, "</select></div>\n");
	buf_html(b,
	    "<table>\n"
	    "<thead><tr><th>%s</th><th>%s</th><th>!bang</th><th>%s</th><th>%s</th><th>%s</th></tr></thead>"
	    "<tbody>\n",
	    tr("Allow"), tr("Engine name"), tr("Pages"), tr("SafeSearch"), tr("Time range"));
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
