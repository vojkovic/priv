#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engines/engine.h"
#include "features/autocomplete.h"
#include "features/favicon.h"
#include "features/prefs.h"
#include "search/locales.h"
#include "search/result.h"
#include "search/search.h"

#define COOKIE "priv"
#define COOKIE_AGE (5 * 365 * 24 * 3600)

/* browser tags that name one of our interface languages differently, most specific first */
static const struct {
	const char *from, *to;
} aliases[] = {
    {"zh-hant", "zh_Hant_TW"},
    {"zh-hans", "zh_Hans_CN"},
    {"zh-tw", "zh_Hant_TW"},
    {"zh-hk", "zh_Hant_TW"},
    {"zh-mo", "zh_Hant_TW"},
    {"zh", "zh_Hans_CN"},
    {"nb", "nb_NO"},
    {"no", "nb_NO"},
    {"nn", "nb_NO"},
    {"el", "el_GR"},
    {"fa", "fa_IR"},
    {"tl", "fil"},
};

const struct theme themes[] = {
    {"dracula", "Dracula"},
    {"everforest", "Everforest"},
    {"frappe", "Frappé"},
    {"gruvbox", "Gruvbox"},
    {"latte", "Latte"},
    {"macchiato", "Macchiato"},
    {"matcha", "Matcha"},
    {"mocha", "Mocha"},
    {"mono", "Mono"},
    {"nord", "Nord"},
    {"slate", "Slate"},
    {"sorbet", "Sorbet"},
    {"sorbetnight", "Sorbet Night"},
};
const size_t nthemes = LEN(themes);

static const struct ui_lang *current;

static int
msgid_cmp(const void *key, const void *elem)
{
	return strcmp(key, *(const char *const *)elem);
}

void
i18n_use(const struct ui_lang *l)
{
	current = l;
}

const char *
tr(const char *msgid)
{
	const char *const *id;

	if (!current || !(id = bsearch(msgid, msgids, nmsgids, sizeof(*msgids), msgid_cmp)))
		return msgid;
	return current->msgs[id - msgids] ? current->msgs[id - msgids] : msgid;
}

static const struct ui_lang *
ui_find(const char *code)
{
	size_t i;

	for (i = 0; code && i < nui_langs; i++)
		if (!strcmp(ui_langs[i].code, code))
			return &ui_langs[i];
	return NULL;
}

static int
tag_is(const char *tag, const char *want)
{
	size_t n = strlen(want);

	return !strncasecmp(tag, want, n) && (!tag[n] || tag[n] == '-');
}

static const struct ui_lang *
ui_accept(struct arena *a, const char *header)
{
	char *items = astrdup(a, header), *item, *save, *p;
	size_t i;

	for (item = strtok_r(items, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
		if ((p = strchr(item, ';')))
			*p = 0;
		item = str_strip(item);
		for (p = item; *p; p++)
			if (*p == '_')
				*p = '-';
		for (i = 0; i < nui_langs; i++)
			if (!strcasecmp(ui_langs[i].tag, item))
				return &ui_langs[i];
		for (i = 0; i < LEN(aliases); i++)
			if (tag_is(item, aliases[i].from))
				return ui_find(aliases[i].to);
		for (i = 0; i < nui_langs; i++)
			if (tag_is(item, ui_langs[i].tag))
				return &ui_langs[i];
	}
	return ui_find("en");
}

static int
valid_language(const char *tag)
{
	size_t i;

	if (!strcmp(tag, "auto"))
		return 1;
	for (i = 0; i < nlocales; i++)
		if (!strcmp(locales[i].tag, tag))
			return 1;
	return 0;
}

static const char *
cookie_value(struct arena *a, const char *header, const char *name)
{
	char *items = astrdup(a, header), *item, *save, *eq;

	for (item = strtok_r(items, ";", &save); item; item = strtok_r(NULL, ";", &save)) {
		item = str_strip(item);
		if ((eq = strchr(item, '=')) && (size_t)(eq - item) == strlen(name) && !strncmp(item, name, eq - item))
			return eq + 1;
	}
	return NULL;
}

static size_t
category_index(const char *name)
{
	size_t i;

	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++)
		if (!strcmp(categories[i].name, name))
			return i;
	return PREFS_CATEGORIES;
}

static const struct engine *
engine_by_shortcut(const char *shortcut)
{
	size_t i;

	for (i = 0; i < nengines; i++)
		if (engines[i]->shortcut && !strcmp(engines[i]->shortcut, shortcut))
			return engines[i];
	return NULL;
}

/* a category.shortcut pair, e.g. music.yt */
static int
pair(struct arena *a, const char *s, size_t *cat, const struct engine **e)
{
	char *v = astrdup(a, s), *dot = strchr(v, '.');

	if (!dot)
		return 0;
	*dot = 0;
	*cat = category_index(v);
	*e = engine_by_shortcut(dot + 1);
	return *cat < PREFS_CATEGORIES && *e && engine_in(*e, categories[*cat].name);
}

/* a name from one of the lists of choices, each of which starts its entries with the name */
static const char *
pick(const char *name, const void *list, size_t n, size_t size)
{
	const char *const *e;
	size_t i;

	for (i = 0; name && i < n; i++) {
		e = (const char *const *)((const char *)list + i * size);
		if (!strcmp(*e, name))
			return *e;
	}
	return NULL;
}

/* a timeout in tenths of a second, 0 for the category's own default */
static int
timeout_pick(const char *s, size_t cat)
{
	size_t i;
	int n;

	if (!s || !*s || cat >= PREFS_CATEGORIES || cat >= ncategories)
		return 0;
	n = (int)parse_long(s, 0);
	if (n == (int)(categories[cat].timeout * 10 + 0.5))
		return 0;
	for (i = 0; i < ntimeout_choices; i++)
		if (n == timeout_choices[i])
			return n;
	return 0;
}

#define THEME(s) pick(s, themes, nthemes, sizeof(*themes))
#define FAVICON(s) pick(s, favicon_resolvers, nfavicon_resolvers, sizeof(*favicon_resolvers))
#define AUTOCOMPLETE(s) pick(s, autocompleters, nautocompleters, sizeof(*autocompleters))

void
prefs_parse(struct arena *a, struct request *r, struct prefs *p)
{
	const char *c = cookie_value(a, r->cookie, COOKIE), *v;
	const struct engine *e;
	char *list, *item, *save;
	size_t cat, i;

	memset(p, 0, sizeof(*p));
	p->ui_code = "";
	p->safesearch = SAFESEARCH_DEFAULT;
	p->theme = THEME_DEFAULT;
	p->favicon = FAVICON_DEFAULT;
	p->autocomplete = AUTOCOMPLETE_DEFAULT;
	if (c) {
		if ((p->ui = ui_find(query_param(a, c, "ui"))))
			p->ui_code = p->ui->code;
		if ((v = query_param(a, c, "language")) && valid_language(v))
			p->language = v;
		if ((v = query_param(a, c, "safesearch")) && v[0] >= '0' && v[0] <= '2' && !v[1])
			p->safesearch = v[0] - '0';
		if ((v = THEME(query_param(a, c, "theme"))))
			p->theme = v;
		if ((v = FAVICON(query_param(a, c, "favicon"))))
			p->favicon = v;
		if ((v = AUTOCOMPLETE(query_param(a, c, "autocomplete"))))
			p->autocomplete = v;
		if ((v = query_param(a, c, "to"))) {
			list = astrdup(a, v);
			for (item = strtok_r(list, "~", &save); item; item = strtok_r(NULL, "~", &save))
				if ((v = strchr(item, '.'))) {
					*(char *)v = 0;
					cat = category_index(item);
					if (cat < PREFS_CATEGORIES)
						p->timeout[cat] = timeout_pick(v + 1, cat);
				}
		}
		if ((v = query_param(a, c, "off"))) {
			p->saved = 1;
			list = astrdup(a, v);
			for (item = strtok_r(list, "~", &save); item; item = strtok_r(NULL, "~", &save))
				if (pair(a, item, &cat, &e))
					p->off[cat] |= ENGINE_BIT(e);
		}
	}
	/* until engines are saved once, the ones marked starts_off stay off */
	for (cat = 0; !p->saved && cat < ncategories && cat < PREFS_CATEGORIES; cat++)
		for (i = 0; i < nengines; i++)
			if (engines[i]->starts_off && engine_in(engines[i], categories[cat].name))
				p->off[cat] |= ENGINE_BIT(engines[i]);
	p->ui_auto = ui_accept(a, r->accept_language);
	if (!p->ui)
		p->ui = p->ui_auto;
}

/* reads a submitted /preferences form */
void
prefs_from_form(struct arena *a, struct request *r, struct prefs *p)
{
	uint64_t on[PREFS_CATEGORIES] = {0};
	const struct engine *e;
	const char *qs = r->query, *end, *eq, *v;
	size_t i, j, cat;
	char *k;

	p->ui_auto = ui_accept(a, r->accept_language);
	p->ui = ui_find(query_param(a, qs, "ui"));
	p->ui_code = p->ui ? p->ui->code : "";
	if (!p->ui)
		p->ui = p->ui_auto;
	v = query_param(a, qs, "language");
	p->language = v && valid_language(v) ? v : NULL;
	v = query_param(a, qs, "safesearch");
	p->safesearch = v && v[0] >= '0' && v[0] <= '2' && !v[1] ? v[0] - '0' : SAFESEARCH_DEFAULT;
	p->theme = (v = THEME(query_param(a, qs, "theme"))) ? v : THEME_DEFAULT;
	p->favicon = (v = FAVICON(query_param(a, qs, "favicon"))) ? v : FAVICON_DEFAULT;
	p->autocomplete = (v = AUTOCOMPLETE(query_param(a, qs, "autocomplete"))) ? v : AUTOCOMPLETE_DEFAULT;
	for (; *qs; qs = *end ? end + 1 : end) {
		end = qs + strcspn(qs, "&");
		eq = memchr(qs, '=', end - qs);
		k = url_decode(astrndup(a, qs, (eq ? eq : end) - qs));
		if (eq && !strcmp(k, "on") && pair(a, url_decode(astrndup(a, eq + 1, end - eq - 1)), &cat, &e))
			on[cat] |= ENGINE_BIT(e);
	}
	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++) {
		p->timeout[i] = timeout_pick(query_param(a, r->query, aprintf(a, "timeout_%s", categories[i].name)), i);
		p->off[i] = 0;
		for (j = 0; j < nengines; j++)
			if (engine_in(engines[j], categories[i].name) && !(on[i] & ENGINE_BIT(engines[j])))
				p->off[i] |= ENGINE_BIT(engines[j]);
	}
}

char *
prefs_cookie(struct arena *a, const struct prefs *p, int secure)
{
	struct buf b = {0}, off = {0}, to = {0};
	char *r;
	size_t i, j;

	if (!p)
		return aprintf(a, "Set-Cookie: " COOKIE "=; Path=/; Max-Age=0; SameSite=Lax; HttpOnly%s\r\n",
		    secure ? "; Secure" : "");
	if (*p->ui_code)
		buf_printf(&b, "&ui=%s", p->ui_code);
	if (p->language)
		buf_printf(&b, "&language=%s", p->language);
	if (p->safesearch != SAFESEARCH_DEFAULT)
		buf_printf(&b, "&safesearch=%d", p->safesearch);
	if (strcmp(p->theme, THEME_DEFAULT))
		buf_printf(&b, "&theme=%s", p->theme);
	if (strcmp(p->favicon, FAVICON_DEFAULT))
		buf_printf(&b, "&favicon=%s", p->favicon);
	if (strcmp(p->autocomplete, AUTOCOMPLETE_DEFAULT))
		buf_printf(&b, "&autocomplete=%s", p->autocomplete);
	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++)
		for (j = 0; j < nengines; j++)
			if (p->off[i] & ENGINE_BIT(engines[j]))
				buf_printf(&off, "%s%s.%s", off.len ? "~" : "", categories[i].name,
				    engines[j]->shortcut);
	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++)
		if (p->timeout[i])
			buf_printf(&to, "%s%s.%d", to.len ? "~" : "", categories[i].name, p->timeout[i]);
	if (to.len)
		buf_printf(&b, "&to=%s", buf_str(&to));
	buf_printf(&b, "&off=%s", buf_str(&off));
	r = aprintf(a, "Set-Cookie: " COOKIE "=%s; Path=/; Max-Age=%d; SameSite=Lax; HttpOnly%s\r\n", buf_str(&b) + 1,
	    COOKIE_AGE, secure ? "; Secure" : "");
	buf_free(&b);
	buf_free(&off);
	buf_free(&to);
	return r;
}

int
prefs_engine_on(const struct prefs *p, const char *category, const struct engine *e)
{
	size_t i = category_index(category);

	return i == PREFS_CATEGORIES || !(p->off[i] & ENGINE_BIT(e));
}
