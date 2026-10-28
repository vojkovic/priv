#include "engines/engine.h"
#include "features/favicon.h"
#include "features/metrics.h"
#include "features/proxy.h"
#include "features/stats.h"
#include "render/icons.h"
#include "render/render.h"
#include "search/search.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

//
// Reading a query and the settings that go with it
//
// the search box takes more than words: !bangs pick engines or a category,
// :languages pick a language and <3 sets a timeout in seconds. query_parse
// takes those out and leaves the words. filters_parse reads the rest of what
// a search depends on (category, language, time range, safesearch) from the
// request and the user's preferences.
//

struct category categories[] = {
    {"general", "General", ICON_SEARCH, 1.5},
    {"images", "Images", ICON_IMAGES, 3},
    {"videos", "Videos", ICON_VIDEOS, 3},
    {"news", "News", ICON_NEWS, 3},
    {"music", "Music", ICON_MUSIC, 3},
    {"tech", "Tech", ICON_TECH, 3},
    {"research", "Research", ICON_RESEARCH, 5},
};

const size_t ncategories = LEN(categories);

const int timeout_choices[] = {5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 100};
const size_t ntimeout_choices = LEN(timeout_choices);

/* the query's own limit, else the user's choice for the category, else the category's default */
double
filters_timeout(const struct filters *f)
{
	size_t i;

	if (f->timeout > 0)
		return f->timeout;
	for (i = 0; i < ncategories; i++)
		if (!strcmp(categories[i].name, f->category))
			return i < PREFS_CATEGORIES && f->prefs.timeout[i] ? f->prefs.timeout[i] / 10.0 :
			                                                     categories[i].timeout;
	return categories[0].timeout;
}

const struct locale *
client_locale(struct arena *a, const char *header)
{
	char *list = astrdup(a, header), *item, *save, *semi, *q, *best = NULL;
	const struct locale *l;
	double bq = -1, qv;
	size_t i;

	for (item = strtok_r(list, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
		qv = 1;
		if ((semi = strchr(item, ';'))) {
			*semi = 0;
			if ((q = strstr(semi + 1, "q=")))
				qv = strtod(q + 2, NULL);
		}
		item = str_strip(item);
		if (!IS_ALPHA(item[0]) || qv <= bq)
			continue;
		bq = qv;
		best = item;
	}
	if (!best)
		return NULL;
	/* auto keeps only the language, en-GB searches as en */
	best[strcspn(best, "-_")] = 0;
	for (i = 0; best[i]; i++)
		best[i] = (char)tolower((unsigned char)best[i]);
	return (l = locale_find(best)) != &locales[0] ? l : NULL;
}

static const char *
category_find(const char *name)
{
	size_t i;

	for (i = 0; name && i < ncategories; i++)
		if (!strcmp(categories[i].name, name))
			return categories[i].name;
	return NULL;
}

static char *
lower(char *s)
{
	char *p;

	for (p = s; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return s;
}

static int
bang(struct arena *a, const char *value, struct filters *f)
{
	char *v = lower(astrdup(a, value)), *p;
	const char *cat;
	struct engine *e;
	size_t i;

	for (p = v; *p; p++)
		if (*p == '_')
			*p = ' ';
	for (i = 0; i < nengines; i++) {
		e = engines[i];
		if ((e->shortcut && !strcmp(e->shortcut, v)) || !strcmp(e->name, v)) {
			if (!f->engines)
				f->category = e->category;
			/* the first bang picks the category, later ones only add engines that belong to it */
			if (engine_in(e, f->category))
				f->engines |= ENGINE_BIT(e);
			return 1;
		}
	}
	if (!(cat = category_find(v)))
		return 0;
	if (!f->engines)
		f->category = cat;
	for (i = 0; i < nengines; i++)
		if (engine_in(engines[i], cat) && engine_in(engines[i], f->category))
			f->engines |= ENGINE_BIT(engines[i]);
	return 1;
}

static int
language(struct arena *a, const char *value, struct filters *f)
{
	char *v = lower(astrdup(a, value)), *p, *name, *country, *english;
	const struct locale *found = NULL;
	size_t i;

	for (p = v; *p; p++)
		if (*p == '_')
			*p = '-';
	for (i = 1; i < nlocales; i++) {
		if (!strcasecmp(locales[i].tag, v)) {
			found = &locales[i];
			break;
		}
		name = lower(astrdup(a, locales[i].names));
		country = strchr(name, '|');
		*country++ = 0;
		english = strchr(country, '|');
		*english++ = 0;
		for (p = country; *p; p++)
			if (*p == ' ')
				*p = '-';
		if ((!strcmp(name, v) || !strcmp(english, v) || !strcmp(country, v)) &&
		    (!found || strchr(found->tag, '-')))
			found = &locales[i];
	}
	if (!found && strcmp(v, "auto"))
		return 0;
	f->language = found ? found->tag : "auto";
	if (found)
		f->locale = found;
	return 1;
}

static int
timeout(const char *value, struct filters *f)
{
	const char *p;
	long n;

	for (p = value; *p; p++)
		if (!IS_DIGIT(*p))
			return 0;
	if (!*value)
		return 0;
	n = parse_long(value, 0);
	f->timeout = MIN(n < 100 ? (double)n : n / 1000.0, 10.0);
	return 1;
}

const char *
query_parse(struct arena *a, const char *raw, struct filters *f)
{
	char *q = astrdup(a, raw), *w, *save;
	struct buf b = {0};
	const char *out;
	int special;

	for (w = strtok_r(q, " \t\n\r", &save); w; w = strtok_r(NULL, " \t\n\r", &save)) {
		special = 0;
		if (w[0] == '<')
			special = timeout(w + 1, f);
		else if (w[0] == ':' && w[1])
			special = language(a, w + 1, f);
		else if (w[0] == '!' && w[1] && w[1] != '!')
			special = bang(a, w + 1, f);
		if (special)
			continue;
		if (b.len)
			buf_putc(&b, ' ');
		buf_puts(&b, w);
	}
	out = astrdup(a, buf_str(&b));
	buf_free(&b);
	return out;
}

void
filters_parse(struct arena *a, struct request *r, struct filters *f)
{
	const char *p, *t;

	prefs_parse(a, r, &f->prefs);
	f->category = category_find(query_param(a, r->query, "categories"));
	if (!f->category)
		f->category = "general";
	f->engines = 0;
	f->timeout = 0;
	p = query_param(a, r->query, "language");
	f->language = p && *p ? p : f->prefs.language ? f->prefs.language : "auto";
	f->browser = client_locale(a, r->accept_language);
	if (!f->browser)
		f->browser = &locales[0];
	if (!strcmp(f->language, "auto")) {
		f->locale = f->browser;
	} else {
		f->locale = locale_find(f->language);
		f->language = f->locale->tag;
	}
	t = query_param(a, r->query, "time_range");
	f->time_range =
	    t && (!strcmp(t, "day") || !strcmp(t, "week") || !strcmp(t, "month") || !strcmp(t, "year")) ? t : NULL;
	p = query_param(a, r->query, "safesearch");
	f->safesearch = p && *p >= '0' && *p <= '2' && !p[1] ? *p - '0' : f->prefs.safesearch;
}
