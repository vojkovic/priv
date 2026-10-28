#include <stdio.h>
#include <string.h>

#include "engines/engine.h"

#define MAIN_WIKI "wiki.archlinux.org"

static const char *
lang_of(struct job *j)
{
	const char *tag = j->filters->locale->tag;

	return astrndup(j->a, tag, strcspn(tag, "-"));
}

static const char *
wiki(const char *lang, const char **title)
{
	static const char *wikis[][3] = {
	    {"de", "wiki.archlinux.de", "Spezial:Suche"},
	    {"ja", "wiki.archlinux.jp", "特別:検索"},
	    {"zh", "wiki.archlinuxcn.org", "Special:搜索"},
	};
	size_t i;

	for (i = 0; i < LEN(wikis); i++) {
		if (!strcmp(wikis[i][0], lang)) {
			*title = wikis[i][2];
			return wikis[i][1];
		}
	}
	*title = "Special:Search";
	return MAIN_WIKI;
}

static const char *
language_name(const char *lang)
{
	static const char *names[][2] = {
	    {"ar", "العربية"},
	    {"bg", "Български"},
	    {"bs", "Bosanski"},
	    {"ca", "Català"},
	    {"cs", "Čeština"},
	    {"da", "Dansk"},
	    {"de", "Deutsch"},
	    {"el", "Ελληνικά"},
	    {"en", "English"},
	    {"es", "Español"},
	    {"fi", "Suomi"},
	    {"fr", "Français"},
	    {"he", "עברית"},
	    {"hr", "Hrvatski"},
	    {"hu", "Magyar"},
	    {"id", "Bahasa Indonesia"},
	    {"it", "Italiano"},
	    {"ja", "日本語"},
	    {"ko", "한국어"},
	    {"lt", "Lietuvių"},
	    {"lv", "Latviešu"},
	    {"nl", "Nederlands"},
	    {"pl", "Polski"},
	    {"pt", "Português"},
	    {"ru", "Русский"},
	    {"sk", "Slovenčina"},
	    {"sr", "Српски / srpski"},
	    {"sv", "Svenska"},
	    {"th", "ไทย"},
	    {"tr", "Türkçe"},
	    {"uk", "Українська"},
	    {"zh", "中文（繁體）"},
	};
	size_t i;

	for (i = 0; i < LEN(names); i++)
		if (!strcmp(names[i][0], lang))
			return names[i][1];
	return "English";
}

static void
response(struct job *j, struct fetch *f)
{
	const char *title, *base = aprintf(j->a, "https://%s/index.php?", wiki(lang_of(j), &title));
	struct xnodes items;
	htmlDocPtr doc;
	char *href;
	int i;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_html(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	items = xpath((xmlNodePtr)doc, "//ul[@class=\"mw-search-results\"]/li");
	for (i = 0; i < items.n; i++) {
		href = xpath_first_text(j->a, items.v[i], ".//div[@class=\"mw-search-result-heading\"]/a/@href");
		if (!href)
			continue;
		add_main(j, url_join(j->a, base, href),
		    xpath_text(j->a, items.v[i], ".//div[@class=\"mw-search-result-heading\"]/a"),
		    xpath_text(j->a, items.v[i], ".//div[@class=\"searchresult\"]"));
	}
	xnodes_free(&items);
	job_done(j);
}

static void
search(struct job *j)
{
	const char *lang = lang_of(j), *title, *netloc = wiki(lang, &title), *query = j->query;
	const char *base = aprintf(j->a, "https://%s/index.php?", netloc);
	struct buf q = {0};
	struct fetch *f;
	char offset[16];

	if (!strcmp(netloc, MAIN_WIKI))
		query = aprintf(j->a, "%s (%s)", query, language_name(lang));
	else if (!strcmp(netloc, "wiki.archlinuxcn.org"))
		base = aprintf(j->a, "https://%s/wzh/index.php?", netloc);
	snprintf(offset, sizeof(offset), "%d", (j->pageno - 1) * 20);
	form_add(&q, "search", query);
	form_add(&q, "title", title);
	form_add(&q, "limit", "20");
	form_add(&q, "offset", offset);
	form_add(&q, "profile", "default");
	f = job_request(j, aprintf(j->a, "%s%s", base, buf_str(&q)));
	buf_free(&q);
	if (!strcmp(netloc, MAIN_WIKI))
		fetch_header(f, "User-Agent: SearXNG/2026.10.21");
	job_send(j, f, response);
}

struct engine engine_archlinux = {
    .name = "arch linux wiki",
    .shortcut = "al",
    .website = "wiki.archlinux.org",
    .bang = "https://wiki.archlinux.org/index.php?search=%s",
    .category = "tech",
    .paging = 1,
    .search = search,
};
