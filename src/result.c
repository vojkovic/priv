#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engine.h"
#include "result.h"

#define MAX_TITLE 200
#define MAX_CONTENT 1200

#define PUSH(arr, n, cap, v) \
	do { \
		if ((n) == (cap)) { \
			(cap) = (cap) ? (cap) * 2 : 16; \
			(arr) = xrealloc((arr), (cap) * sizeof(*(arr))); \
		} \
		(arr)[(n)++] = (v); \
	} while (0)

static char *
default_scheme(struct arena *a, const char *url)
{
	struct url u;

	url_parse(a, url, &u);
	if (!u.scheme[0])
		u.scheme = "http";
	return url_unparse(a, &u);
}

static int
normalize_main(struct arena *a, struct main_result *m)
{
	if (!m->url || !*m->url)
		return -1;
	url_parse(a, m->url, &m->parsed);
	if (starts_with(m->parsed.netloc, "xn--"))
		m->parsed.netloc = idna_decode(a, m->parsed.netloc);
	if (!m->parsed.scheme[0])
		m->parsed.scheme = "http";
	if (strcasecmp(m->parsed.scheme, "http") && strcasecmp(m->parsed.scheme, "https"))
		return -1;
	m->url = url_unparse(a, &m->parsed);
	m->title = text_normalize(a, m->title, MAX_TITLE);
	m->content = text_normalize(a, m->content, MAX_CONTENT);
	if (!strcmp(m->content, m->title))
		m->content = "";
	if (!m->template)
		m->template = "default";
	m->engines = ENGINE_BIT(m->base.engine);
	return 0;
}

static int
hex(int c)
{
	return IS_DIGIT(c) ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
}

static int
next_byte(const char **s)
{
	const char *p = *s;

	if (p[0] == '%' && hex(p[1]) >= 0 && hex(p[2]) >= 0) {
		*s += 3;
		return hex(p[1]) << 4 | hex(p[2]);
	}
	*s += 1;
	return (unsigned char)p[0];
}

/* engines escape urls differently */
static int
same_part(const char *a, const char *b)
{
	while (*a && *b)
		if (next_byte(&a) != next_byte(&b))
			return 0;
	return !*a && !*b;
}

static int
same_main(const struct main_result *x, const struct main_result *y)
{
	const struct url *a = &x->parsed, *b = &y->parsed;

	return !strcmp(x->template, y->template) && !strcasecmp(a->netloc, b->netloc) && same_part(a->path, b->path) &&
	    same_part(a->params, b->params) && same_part(a->query, b->query) && same_part(a->fragment, b->fragment) &&
	    !strcmp(x->img_src ? x->img_src : "", y->img_src ? y->img_src : "");
}

static int
scheme_secure(const char *s)
{
	size_t n = strlen(s);

	return n && s[n - 1] == 's';
}

#define DEFAULT(field) \
	do { \
		if ((!o->field || !*o->field) && m->field && *m->field) \
			o->field = m->field; \
	} while (0)

static void
defaults_from(struct main_result *o, struct main_result *m)
{
	DEFAULT(thumbnail);
	DEFAULT(img_src);
	DEFAULT(pubdate);
	DEFAULT(thumbnail_src);
	DEFAULT(resolution);
	DEFAULT(source);
	DEFAULT(img_format);
	DEFAULT(length);
	DEFAULT(author);
	DEFAULT(views);
	DEFAULT(pdf_url);
	if (!o->attrs)
		o->attrs = m->attrs;
}

static void
merge_main(struct results *r, struct main_result *m, int position)
{
	struct main_result *o;
	size_t i;
	int *pos;

	for (i = 0; i < r->nmain; i++) {
		o = r->main[i];
		if (!same_main(o, m))
			continue;
		if (strlen(m->content) > strlen(o->content))
			o->content = m->content;
		if (strlen(m->title) > strlen(o->title))
			o->title = m->title;
		defaults_from(o, m);
		o->engines |= m->engines;
		if (!scheme_secure(o->parsed.scheme) && scheme_secure(m->parsed.scheme)) {
			o->parsed.scheme = m->parsed.scheme;
			o->url = url_unparse(r->a, &o->parsed);
		}
		pos = amalloc(r->a, (o->npositions + 1) * sizeof(*pos));
		memcpy(pos, o->positions, o->npositions * sizeof(*pos));
		pos[o->npositions++] = position;
		o->positions = pos;
		return;
	}
	m->positions = amalloc(r->a, sizeof(*m->positions));
	m->positions[0] = position;
	m->npositions = 1;
	PUSH(r->main, r->nmain, r->capmain, m);
}

static int
attr_known(const struct infobox_attr *list, const struct infobox_attr *last, const char *name)
{
	const struct infobox_attr *a;

	if (!name || !*name)
		return 0;
	for (a = list; a; a = a == last ? NULL : a->next)
		if ((a->label && !strcmp(a->label, name)) || (a->entity && !strcmp(a->entity, name)))
			return 1;
	return 0;
}

static void
merge_infoboxes(struct arena *ar, struct infobox *o, struct infobox *n)
{
	struct infobox_url *u2, *u1, **tail, *cu;
	struct infobox_attr *a2, *last, **atail, *ca;
	int unique;

	o->engines |= n->engines;

	for (u2 = n->urls; u2; u2 = u2->next) {
		unique = 1;
		for (tail = &o->urls; (u1 = *tail); tail = &u1->next)
			if ((u2->entity && u1->entity && !strcmp(u2->entity, u1->entity)) ||
			    !strcmp(u1->url ? u1->url : "", u2->url ? u2->url : ""))
				unique = 0;
		if (unique) {
			cu = amalloc(ar, sizeof(*cu));
			*cu = *u2;
			cu->next = NULL;
			*tail = cu;
		}
	}

	if (!o->description || !*o->description)
		o->description = n->description;
	if (!o->also)
		o->also = n->also;
	if (n->img_src && *n->img_src && (!o->img_src || !*o->img_src))
		o->img_src = n->img_src;

	if (n->attrs && !o->attrs) {
		o->attrs = n->attrs;
	} else if (n->attrs) {
		for (last = o->attrs; last->next; last = last->next)
			;
		atail = &last->next;
		for (a2 = n->attrs; a2; a2 = a2->next) {
			if (attr_known(o->attrs, last, a2->label) || attr_known(o->attrs, last, a2->entity))
				continue;
			ca = amalloc(ar, sizeof(*ca));
			*ca = *a2;
			ca->next = NULL;
			*atail = ca;
			atail = &ca->next;
		}
	}

	if (n->content && *n->content && (!o->content || !*o->content || strlen(n->content) > strlen(o->content)))
		o->content = n->content;
}

static void
merge_infobox(struct results *r, struct infobox *ib)
{
	struct infobox_url *u;
	size_t i;
	int added = 0;

	for (u = ib->urls; u; u = u->next)
		if (u->url && *u->url)
			u->url = default_scheme(r->a, u->url);
	ib->engines = ENGINE_BIT(ib->base.engine);
	if (ib->id && *ib->id) {
		ib->id = default_scheme(r->a, ib->id);
		for (i = 0; i < r->ninfoboxes; i++) {
			if (r->infoboxes[i]->id && !strcmp(r->infoboxes[i]->id, ib->id)) {
				merge_infoboxes(r->a, r->infoboxes[i], ib);
				added = 1;
			}
		}
	}
	if (!added)
		PUSH(r->infoboxes, r->ninfoboxes, r->capinfoboxes, ib);
}

void
results_extend(struct results *r, const struct engine *e, struct result *list)
{
	struct result *res, *next;
	struct main_result *m;
	struct answer *an;
	int count = 0;
	size_t i;

	for (res = list; res; res = next) {
		next = res->next;
		res->next = NULL;
		res->engine = e;
		switch (res->type) {
		case RESULT_MAIN:
			m = (struct main_result *)res;
			if (normalize_main(r->a, m) < 0)
				continue;
			merge_main(r, m, ++count);
			break;
		case RESULT_INFOBOX: merge_infobox(r, (struct infobox *)res); break;
		case RESULT_ANSWER:
			an = (struct answer *)res;
			for (i = 0; i < r->nanswers; i++)
				if (!strcmp(r->answers[i]->answer, an->answer))
					break;
			if (i == r->nanswers)
				PUSH(r->answers, r->nanswers, r->capanswers, an);
			break;
		}
	}
	if (e->paging)
		r->paging = 1;
}

void
results_unresponsive(struct results *r, const struct engine *e, const char *error, int suspended)
{
	struct unresponsive u = {e, error, suspended};

	PUSH(r->unresponsive, r->nunresponsive, r->capunresponsive, u);
}

void
results_timing(struct results *r, const struct engine *e, double seconds, double load)
{
	struct timing t = {e, seconds, load};

	PUSH(r->timings, r->ntimings, r->captimings, t);
}

static double
score(struct main_result *m)
{
	double s = 0;
	int p;

	for (p = 0; p < m->npositions; p++)
		s += (double)m->npositions / m->positions[p];
	return s;
}

static void
sort_by(void **v, size_t n, int (*before)(const void *, const void *))
{
	size_t i, j;
	void *x;

	for (i = 1; i < n; i++) {
		x = v[i];
		for (j = i; j > 0 && before(x, v[j - 1]); j--)
			v[j] = v[j - 1];
		v[j] = x;
	}
}

static int
higher_score(const void *a, const void *b)
{
	return ((const struct main_result *)a)->score > ((const struct main_result *)b)->score;
}

static int
infobox_rank(const struct infobox *ib)
{
	return (ib->description && *ib->description ? 2 : 0) + (ib->img_src && *ib->img_src ? 1 : 0);
}

static int
has_image(const void *a, const void *b)
{
	return infobox_rank(a) > infobox_rank(b);
}

struct group {
	const char *key;
	size_t index;
	int count;
};

void
results_close(struct results *r)
{
	struct main_result **out, *m;
	struct group *groups;
	size_t i, j, n = 0, ngroups = 0;
	const char *key;

	sort_by((void **)r->infoboxes, r->ninfoboxes, has_image);
	for (i = 0; i < r->nmain; i++)
		r->main[i]->score = score(r->main[i]);
	sort_by((void **)r->main, r->nmain, higher_score);

	out = amalloc(r->a, (r->nmain + 1) * sizeof(*out));
	groups = amalloc(r->a, (r->nmain + 1) * sizeof(*groups));
	for (i = 0; i < r->nmain; i++) {
		m = r->main[i];
		m->category = m->base.engine->categories[0];
		key = aprintf(r->a, "%s:%s:%s", m->category, m->template,
		    (m->thumbnail && *m->thumbnail) || (m->img_src && *m->img_src) ? "img_src" : "");
		for (j = 0; j < ngroups && strcmp(groups[j].key, key); j++)
			;
		if (j < ngroups && groups[j].count > 0 && n - groups[j].index < 20) {
			size_t index = groups[j].index, k;
			memmove(out + index + 1, out + index, (n - index) * sizeof(*out));
			out[index] = m;
			n++;
			for (k = 0; k < ngroups; k++)
				if (groups[k].index >= index)
					groups[k].index++;
			groups[j].count--;
		} else {
			out[n++] = m;
			if (j == ngroups)
				ngroups++;
			groups[j].key = key;
			groups[j].index = n;
			groups[j].count = 8;
		}
	}
	if (n)
		memcpy(r->main, out, n * sizeof(*out));
}
