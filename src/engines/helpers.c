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

static void
add(struct job *j, struct result *r, enum result_type type)
{
	r->type = type;
	r->engine = j->e;
	r->next = NULL;
	*j->tail = r;
	j->tail = &r->next;
}

struct main_result *
add_main(struct job *j, const char *url, const char *title, const char *content)
{
	struct main_result *m = amalloc(j->a, sizeof(*m));

	memset(m, 0, sizeof(*m));
	m->url = astrdup(j->a, url ? url : "");
	m->title = astrdup(j->a, title ? title : "");
	m->content = astrdup(j->a, content ? content : "");
	add(j, &m->base, RESULT_MAIN);
	return m;
}

struct main_result *
add_image(struct job *j, const char *url, const char *title, const char *img_src)
{
	struct main_result *m = add_main(j, url, title, "");

	m->template = "images";
	m->img_src = astrdup(j->a, img_src ? img_src : "");
	m->thumbnail_src = m->resolution = m->source = m->img_format = "";
	return m;
}

struct main_result *
add_video(struct job *j, const char *url, const char *title, const char *content)
{
	struct main_result *m = add_main(j, url, title, content);

	m->template = "videos";
	return m;
}

struct main_result *
add_paper(struct job *j, const char *url, const char *title, const char *content)
{
	struct main_result *m = add_main(j, url, title, content);

	m->template = "paper";
	return m;
}

void
result_attr(struct job *j, struct main_result *m, const char *label, const char *value)
{
	struct result_attr *at, **tail;

	if (!value || !*value)
		return;
	at = amalloc(j->a, sizeof(*at));
	at->label = label;
	at->value = astrdup(j->a, value);
	at->next = NULL;
	for (tail = &m->attrs; *tail; tail = &(*tail)->next)
		;
	*tail = at;
}

struct infobox *
add_infobox(struct job *j, const char *title)
{
	struct infobox *ib = amalloc(j->a, sizeof(*ib));

	memset(ib, 0, sizeof(*ib));
	ib->title = astrdup(j->a, title);
	add(j, &ib->base, RESULT_INFOBOX);
	return ib;
}

void
infobox_url(struct job *j, struct infobox *ib, const char *title, const char *url, const char *entity, int official)
{
	struct infobox_url *u, **tail;

	if (!web_url(url))
		return;
	u = amalloc(j->a, sizeof(*u));
	u->title = astrdup(j->a, title ? title : "");
	u->url = astrdup(j->a, url);
	u->entity = entity ? astrdup(j->a, entity) : NULL;
	u->official = official;
	u->next = NULL;
	for (tail = &ib->urls; *tail; tail = &(*tail)->next)
		;
	*tail = u;
}

void
infobox_also(struct job *j, struct infobox *ib, const char *title, const char *description)
{
	struct infobox_url *u = amalloc(j->a, sizeof(*u)), **tail;

	memset(u, 0, sizeof(*u));
	u->title = astrdup(j->a, title);
	u->entity = description && *description ? astrdup(j->a, description) : NULL;
	for (tail = &ib->also; *tail; tail = &(*tail)->next)
		;
	*tail = u;
}

void
infobox_attr(struct job *j, struct infobox *ib, const char *label, const char *value, const char *entity)
{
	struct infobox_attr *a = amalloc(j->a, sizeof(*a)), **tail;

	a->label = label ? astrdup(j->a, label) : NULL;
	a->value = astrdup(j->a, value ? value : "");
	a->entity = entity ? astrdup(j->a, entity) : NULL;
	a->next = NULL;
	for (tail = &ib->attrs; *tail; tail = &(*tail)->next)
		;
	*tail = a;
}

void
add_answer(struct job *j, const char *answer, const char *url)
{
	struct answer *a = amalloc(j->a, sizeof(*a));

	memset(a, 0, sizeof(*a));
	a->answer = astrdup(j->a, answer);
	a->url = web_url(url) ? astrdup(j->a, url) : NULL;
	add(j, &a->base, RESULT_ANSWER);
}

struct definition *
add_definition(struct job *j, const char *word, const char *url)
{
	struct infobox *ib = add_infobox(j, word);

	ib->definition = amalloc(j->a, sizeof(*ib->definition));
	memset(ib->definition, 0, sizeof(*ib->definition));
	ib->definition->word = astrdup(j->a, word);
	ib->definition->tail = &ib->definition->classes;
	infobox_url(j, ib, "Wiktionary", url, NULL, 0);
	return ib->definition;
}

struct word_class *
definition_class(struct job *j, struct definition *d, const char *name)
{
	struct word_class *c = amalloc(j->a, sizeof(*c));

	memset(c, 0, sizeof(*c));
	c->name = astrdup(j->a, name);
	c->tail = &c->senses;
	*d->tail = c;
	d->tail = &c->next;
	return c;
}

void
definition_sense(struct job *j, struct word_class *c, const char *text, const char *example)
{
	struct sense *s = amalloc(j->a, sizeof(*s));

	memset(s, 0, sizeof(*s));
	s->text = astrdup(j->a, text);
	s->example = *example ? astrdup(j->a, example) : NULL;
	*c->tail = s;
	c->tail = &s->next;
}

void
job_on_cleanup(struct job *j, void (*fn)(void *), void *p)
{
	struct cleanup *grown;

	/* grows in the search's arena, so an engine can parse as many documents as it needs */
	if (j->ncleanup == j->capcleanup) {
		j->capcleanup = j->capcleanup ? j->capcleanup * 2 : 8;
		grown = amalloc(j->a, (size_t)j->capcleanup * sizeof(*grown));
		if (j->ncleanup)
			memcpy(grown, j->cleanup, (size_t)j->ncleanup * sizeof(*grown));
		j->cleanup = grown;
	}
	j->cleanup[j->ncleanup].fn = fn;
	j->cleanup[j->ncleanup++].p = p;
}

static void
free_html(void *p)
{
	xmlFreeDoc(p);
}

static void
free_json(void *p)
{
	yyjson_doc_free(p);
}

htmlDocPtr
job_html(struct job *j, struct fetch *f)
{
	const char *s = fetch_body(f);
	htmlDocPtr d;

	if (starts_with(s, "<?xml") && strstr(s, "?>"))
		s = strstr(s, "?>") + 2;
	d = html_parse(s, strlen(s));
	if (d)
		job_on_cleanup(j, free_html, d);
	return d;
}

xmlDocPtr
job_xml(struct job *j, struct fetch *f)
{
	xmlDocPtr d = xml_parse(f->body.p ? f->body.p : "", f->body.len);

	if (d)
		job_on_cleanup(j, free_html, d);
	return d;
}

yyjson_doc *
job_json(struct job *j, const char *s, size_t len)
{
	yyjson_doc *d = yyjson_read(s, len, YYJSON_READ_ALLOW_INVALID_UNICODE);

	if (d)
		job_on_cleanup(j, free_json, d);
	return d;
}

const char *
json_text(struct arena *a, yyjson_val *obj, const char *key)
{
	yyjson_val *v = yyjson_obj_get(obj, key);

	if (yyjson_is_str(v))
		return yyjson_get_str(v);
	if (yyjson_is_int(v))
		return aprintf(a, "%lld", (long long)yyjson_get_sint(v));
	if (yyjson_is_real(v))
		return aprintf(a, "%g", yyjson_get_real(v));
	return "";
}

const char *
json_str(yyjson_val *obj, const char *key)
{
	const char *s = yyjson_get_str(yyjson_obj_get(obj, key));

	return s ? s : "";
}

const char *
format_duration(struct arena *a, long secs)
{
	if (secs <= 0)
		return "";
	if (secs >= 3600)
		return aprintf(a, "%ld:%02ld:%02ld", secs / 3600, secs / 60 % 60, secs % 60);
	return aprintf(a, "%ld:%02ld", secs / 60, secs % 60);
}

const char *
format_date(struct arena *a, const char *iso)
{
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov",
	    "Dec"};
	int y, mo, d;

	if (!iso || sscanf(iso, "%4d-%2d-%2d", &y, &mo, &d) != 3 || mo < 1 || mo > 12) // NOLINT(cert-err34-c)
		return iso ? astrdup(a, iso) : "";
	return aprintf(a, "%s %d, %d", months[mo - 1], d, y);
}

const char *
format_count(struct arena *a, long long n)
{
	if (n >= 1000000000)
		return aprintf(a, "%.1fB", n / 1e9);
	if (n >= 1000000)
		return aprintf(a, "%.1fM", n / 1e6);
	if (n >= 1000)
		return aprintf(a, "%.1fK", n / 1e3);
	return aprintf(a, "%lld", n);
}

const char *
time_range_code(const struct filters *f, const char *day, const char *week, const char *month, const char *year)
{
	const char *ranges[] = {"day", "week", "month", "year"};
	const char *codes[] = {day, week, month, year};
	size_t i;

	for (i = 0; f->time_range && i < LEN(ranges); i++)
		if (!strcmp(f->time_range, ranges[i]))
			return codes[i];
	return NULL;
}
