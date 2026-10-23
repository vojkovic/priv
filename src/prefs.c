#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engine.h"
#include "locales.h"
#include "prefs.h"
#include "result.h"
#include "search.h"

#define COOKIE "privy"
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

void
prefs_parse(struct arena *a, struct request *r, struct prefs *p)
{
	const char *c = cookie_value(a, r->cookie, COOKIE), *v;
	const struct engine *e;
	char *list, *item, *save;
	size_t cat;

	memset(p, 0, sizeof(*p));
	p->ui_code = "";
	p->safesearch = -1;
	if (c) {
		if ((p->ui = ui_find(query_param(a, c, "ui"))))
			p->ui_code = p->ui->code;
		if ((v = query_param(a, c, "language")) && valid_language(v))
			p->language = v;
		if ((v = query_param(a, c, "safesearch")) && v[0] >= '0' && v[0] <= '2' && !v[1])
			p->safesearch = v[0] - '0';
		if ((v = query_param(a, c, "off"))) {
			list = astrdup(a, v);
			for (item = strtok_r(list, "~", &save); item; item = strtok_r(NULL, "~", &save))
				if (pair(a, item, &cat, &e))
					p->off[cat] |= ENGINE_BIT(e);
		}
	}
	p->ui_auto = ui_accept(a, r->accept_language);
	if (!p->ui)
		p->ui = p->ui_auto;
}

/* reads a submitted /preferences form, returning the label of a category with too many engines */
const char *
prefs_from_form(struct arena *a, struct request *r, struct prefs *p)
{
	uint64_t on[PREFS_CATEGORIES] = {0};
	const struct engine *e;
	const char *qs = r->query, *end, *eq, *v;
	size_t i, j, cat, n;
	char *k;

	p->ui_auto = ui_accept(a, r->accept_language);
	p->ui = ui_find(query_param(a, qs, "ui"));
	p->ui_code = p->ui ? p->ui->code : "";
	if (!p->ui)
		p->ui = p->ui_auto;
	v = query_param(a, qs, "language");
	p->language = v && valid_language(v) ? v : NULL;
	v = query_param(a, qs, "safesearch");
	p->safesearch = v && v[0] >= '0' && v[0] <= '2' && !v[1] ? v[0] - '0' : -1;
	for (; *qs; qs = *end ? end + 1 : end) {
		end = qs + strcspn(qs, "&");
		eq = memchr(qs, '=', end - qs);
		k = url_decode(astrndup(a, qs, (eq ? eq : end) - qs));
		if (eq && !strcmp(k, "on") && pair(a, url_decode(astrndup(a, eq + 1, end - eq - 1)), &cat, &e))
			on[cat] |= ENGINE_BIT(e);
	}
	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++) {
		p->off[i] = 0;
		for (j = n = 0; j < nengines; j++) {
			if (!engine_in(engines[j], categories[i].name))
				continue;
			if (on[i] & ENGINE_BIT(engines[j]))
				n++;
			else
				p->off[i] |= ENGINE_BIT(engines[j]);
		}
		if (n > MAX_CATEGORY_ENGINES)
			return categories[i].label;
	}
	return NULL;
}

char *
prefs_cookie(struct arena *a, const struct prefs *p, int secure)
{
	struct buf b = {0}, off = {0};
	char *r;
	size_t i, j;

	if (*p->ui_code)
		buf_printf(&b, "&ui=%s", p->ui_code);
	if (p->language)
		buf_printf(&b, "&language=%s", p->language);
	if (p->safesearch >= 0)
		buf_printf(&b, "&safesearch=%d", p->safesearch);
	for (i = 0; i < ncategories && i < PREFS_CATEGORIES; i++)
		for (j = 0; j < nengines; j++)
			if (p->off[i] & ENGINE_BIT(engines[j]))
				buf_printf(&off, "%s%s.%s", off.len ? "~" : "", categories[i].name,
				    engines[j]->shortcut);
	if (off.len)
		buf_printf(&b, "&off=%s", buf_str(&off));
	r = aprintf(a, "Set-Cookie: " COOKIE "=%s; Path=/; Max-Age=%d; SameSite=Lax; HttpOnly%s\r\n",
	    b.len ? buf_str(&b) + 1 : "", b.len ? COOKIE_AGE : 0, secure ? "; Secure" : "");
	buf_free(&b);
	buf_free(&off);
	return r;
}

int
prefs_engine_on(const struct prefs *p, const char *category, const struct engine *e)
{
	size_t i = category_index(category);

	return i == PREFS_CATEGORIES || !(p->off[i] & ENGINE_BIT(e));
}
