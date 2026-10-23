#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../lib/yyjson/yyjson.h"
#include "autocomplete.h"
#include "engine.h"
#include "fetch.h"
#include "js.h"
#include "proxy.h"
#include "search.h"

struct suggestion {
	char *text, *icon, *description;
	int trending;
};

struct pending {
	struct conn *c;
	struct fetch *f;
	struct arena a;
	char *query, *origin, *prefix;
	int json, firefox, chromium;
};

#define MAX_LOCAL 12

static void
pending_free(struct pending *p)
{
	fetch_free(p->f);
	arena_free(&p->a);
	free(p);
}

static void
on_close(void *arg)
{
	pending_free(arg);
}

static const char *
hl(struct arena *a, const char *tag)
{
	const char *dash;

	if (!tag || !*tag || !strcmp(tag, "all") || !strcmp(tag, "auto"))
		return "en";
	dash = strchr(tag, '-');
	if (!strncmp(tag, "zh", 2) && (!tag[2] || tag[2] == '-'))
		return dash ? aprintf(a, "zh-%s", dash + 1) : "zh-CN";
	return dash ? astrndup(a, tag, dash - tag) : tag;
}

static char *
normalize_icon(struct arena *a, const char *url)
{
	char *r, *s;
	size_t n;

	while (*url == ' ')
		url++;
	if (starts_with(url, "data:"))
		return astrdup(a, url);
	if (starts_with(url, "//"))
		url = aprintf(a, "https:%s", url);
	if (!starts_with(url, "http://") && !starts_with(url, "https://"))
		return NULL;
	r = astrdup(a, url);
	if (strstr(r, "encrypted-tbn0.gstatic.com") && (s = strstr(r, "&s="))) {
		for (n = 3; IS_DIGIT(s[n]); n++)
			;
		if (n > 3)
			r = aprintf(a, "%.*s&s=64%s", (int)(s - r), r, s + n);
	}
	return r;
}

static size_t
parse(struct pending *p, const char *body, struct suggestion **out)
{
	const char *start = strchr(body, '[');
	struct suggestion *v;
	yyjson_doc *d;
	yyjson_val *list, *item, *meta, *zp;
	size_t end, i, n, count = 0;
	const char *s;

	*out = NULL;
	if (!start || (end = js_match(start, strlen(start), 0, '[', ']')) == JS_NOMATCH)
		return 0;
	if (!(d = yyjson_read(start, end + 1, YYJSON_READ_ALLOW_INVALID_UNICODE)))
		return 0;
	list = yyjson_arr_get(yyjson_doc_get_root(d), 0);
	v = amalloc(&p->a, (yyjson_arr_size(list) + 1) * sizeof(*v));
	yyjson_arr_foreach (list, i, n, item) {
		if (!(s = yyjson_get_str(yyjson_arr_get(item, 0))) || !*(s = html_to_text(&p->a, s)))
			continue;
		memset(&v[count], 0, sizeof(*v));
		v[count].text = (char *)s;
		meta = yyjson_arr_get(item, 3);
		if (yyjson_is_obj(meta)) {
			if ((s = yyjson_get_str(yyjson_obj_get(meta, "zs"))))
				v[count].icon = normalize_icon(&p->a, s);
			if ((s = yyjson_get_str(yyjson_obj_get(meta, "zi"))) && *s)
				v[count].description = html_to_text(&p->a, s);
			zp = yyjson_obj_get(meta, "zp");
			v[count].trending =
			    yyjson_is_obj(zp) && (s = yyjson_get_str(yyjson_obj_get(zp, "gs_ss"))) && !strcmp(s, "1");
		}
		count++;
	}
	yyjson_doc_free(d);
	*out = v;
	return count;
}

static void
varint(struct buf *b, uint64_t v)
{
	while (v > 0x7F) {
		buf_putc(b, (char)((v & 0x7F) | 0x80));
		v >>= 7;
	}
	buf_putc(b, (char)v);
}

static void
proto_bytes(struct buf *b, int field, const char *p, size_t n)
{
	varint(b, (uint64_t)field << 3 | 2);
	varint(b, n);
	buf_add(b, p, n);
}

static char *
suggest_template(struct arena *a, const char *image, const char *description)
{
	struct buf b = {0}, inner = {0};
	char *r;

	varint(&b, 1 << 3);
	varint(&b, 2);
	if (description && *description) {
		proto_bytes(&inner, 1, description, strlen(description));
		proto_bytes(&b, 4, inner.p, inner.len);
		inner.len = 0;
	}
	if (image && *image) {
		proto_bytes(&inner, 1, image, strlen(image));
		varint(&inner, 3 << 3);
		varint(&inner, 2);
		proto_bytes(&b, 5, inner.p, inner.len);
	}
	r = base64(a, (unsigned char *)b.p, b.len);
	buf_free(&b);
	buf_free(&inner);
	return r;
}

static char *
proxied(struct pending *p, const char *icon, int absolute)
{
	char *u = image_url(&p->a, icon);

	if (!u || starts_with(u, "data:") || !absolute)
		return u;
	return aprintf(&p->a, "%s%s", p->origin, u);
}

static char *
render_json(struct pending *p, struct suggestion *v, size_t n)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_arr(d), *list = yyjson_mut_arr(d), *o;
	char *icon, *r;
	size_t i;

	yyjson_mut_doc_set_root(d, root);
	yyjson_mut_arr_add_str(d, root, p->query);
	yyjson_mut_arr_append(root, list);
	for (i = 0; i < n; i++) {
		icon = v[i].icon ? proxied(p, v[i].icon, 0) : NULL;
		if (!icon && !v[i].description && !v[i].trending) {
			yyjson_mut_arr_add_str(d, list, v[i].text);
			continue;
		}
		o = yyjson_mut_arr_add_obj(d, list);
		yyjson_mut_obj_add_str(d, o, "text", v[i].text);
		if (icon)
			yyjson_mut_obj_add_str(d, o, "icon", icon);
		if (v[i].description)
			yyjson_mut_obj_add_str(d, o, "description", v[i].description);
		if (v[i].trending)
			yyjson_mut_obj_add_bool(d, o, "trending", 1);
	}
	r = yyjson_mut_write(d, 0, NULL);
	yyjson_mut_doc_free(d);
	return r;
}

static char *
render_omnibox(struct pending *p, struct suggestion *v, size_t n)
{
	yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
	yyjson_mut_val *root = yyjson_mut_arr(d), *texts, *descs, *extras, *rel, *details, *types, *o, *cd;
	int any = 0;
	char *icon, *r;
	size_t i;

	yyjson_mut_doc_set_root(d, root);
	yyjson_mut_arr_add_str(d, root, p->query);
	texts = yyjson_mut_arr_add_arr(d, root);
	descs = yyjson_mut_arr_add_arr(d, root);
	yyjson_mut_arr_add_arr(d, root);
	extras = yyjson_mut_arr_add_obj(d, root);
	rel = yyjson_mut_obj_add_arr(d, extras, "google:suggestrelevance");
	details = yyjson_mut_arr(d);
	for (i = 0; i < n; i++) {
		yyjson_mut_arr_add_str(d, texts, v[i].text);
		yyjson_mut_arr_add_int(d, rel, 600 - (int)i);
		yyjson_mut_arr_add_str(d, descs, p->firefox && v[i].description ? v[i].description : "");
		o = yyjson_mut_arr_add_obj(d, details);
		icon = v[i].icon ? proxied(p, v[i].icon, 1) : NULL;
		if (!icon)
			continue;
		any = 1;
		if (p->firefox) {
			yyjson_mut_obj_add_str(d, o, "i", icon);
			if (v[i].description)
				yyjson_mut_obj_add_str(d, o, "a", v[i].description);
		} else
			yyjson_mut_obj_add_str(d, o, "google:suggesttemplate",
			    suggest_template(&p->a, icon, v[i].description));
	}
	if (!p->firefox) {
		yyjson_mut_obj_add_int(d, extras, "google:verbatimrelevance", 1300);
		types = yyjson_mut_obj_add_arr(d, extras, "google:suggesttype");
		for (i = 0; i < n; i++)
			yyjson_mut_arr_add_str(d, types, "QUERY");
		if (p->chromium)
			any = 0;
		else {
			cd = yyjson_mut_obj_add_obj(d, extras, "google:clientdata");
			yyjson_mut_obj_add_bool(d, cd, "bpc", 0);
			yyjson_mut_obj_add_bool(d, cd, "tlw", 0);
		}
	}
	if (any)
		yyjson_mut_obj_add_val(d, extras, "google:suggestdetail", details);
	r = yyjson_mut_write(d, 0, NULL);
	yyjson_mut_doc_free(d);
	return r;
}

static void
respond(struct pending *p, struct suggestion *v, size_t n)
{
	char *body = p->json ? render_json(p, v, n) : render_omnibox(p, v, n);

	http_respond(p->c, 200,
	    p->json ?
	        "Content-Type: application/json\r\nCache-Control: no-store\r\nVary: Accept, User-Agent\r\n" :
	        "Content-Type: application/x-suggestions+json\r\n"
	        "Cache-Control: private, max-age=3600, stale-while-revalidate=300\r\nVary: Accept, User-Agent\r\n",
	    body, strlen(body));
	free(body);
	pending_free(p);
}

static void
done(struct fetch *f, void *arg)
{
	struct pending *p = arg;
	struct suggestion *v = NULL;
	size_t i, n = 0;

	http_on_close(p->c, NULL, NULL);
	if (f->code == CURLE_OK && f->status == 200)
		n = parse(p, fetch_body(f), &v);
	for (i = 0; *p->prefix && i < n; i++)
		v[i].text = aprintf(&p->a, "%s %s", p->prefix, v[i].text);
	respond(p, v, n);
}

static int
special(const char *w)
{
	return w[0] == '!' || w[0] == ':' || w[0] == '<';
}

static char *
lower(struct arena *a, const char *s, char from, char to)
{
	char *r = astrdup(a, s), *c;

	for (c = r; *c; c++)
		*c = *c == from ? to : (char)tolower((unsigned char)*c);
	return r;
}

static size_t
add(struct pending *p, struct suggestion *v, size_t n, const char *before, const char *text, const char *description)
{
	size_t i;

	text = aprintf(&p->a, "%s%s", before, text);
	for (i = 0; i < n; i++)
		if (!strcmp(v[i].text, text))
			return n;
	if (n == MAX_LOCAL)
		return n;
	memset(&v[n], 0, sizeof(*v));
	v[n].text = (char *)text;
	v[n].description = description ? astrdup(&p->a, description) : NULL;
	return n + 1;
}

static size_t
complete_bang(struct pending *p, struct suggestion *v, const char *before, const char *word)
{
	char *value = lower(&p->a, word, '_', ' '), *c;
	struct engine *e;
	size_t i, n = 0, len;

	for (c = value; *c; c++)
		if (*c == '-')
			*c = ' ';
	len = strlen(value);
	for (i = 0; i < ncategories; i++)
		if (!strncmp(categories[i].name, value, len))
			n = add(p, v, n, before, aprintf(&p->a, "!%s", categories[i].name), categories[i].label);
	for (i = 0; len && i < nengines; i++) {
		e = engines[i];
		if (e->shortcut && !strncmp(e->shortcut, value, len))
			n = add(p, v, n, before, aprintf(&p->a, "!%s", e->shortcut), e->name);
	}
	for (i = 0; len && i < nengines; i++) {
		e = engines[i];
		if (!strncmp(e->name, value, len))
			n = add(p, v, n, before, aprintf(&p->a, "!%s", lower(&p->a, e->name, ' ', '_')), e->name);
	}
	return n;
}

static size_t
complete_language(struct pending *p, struct suggestion *v, const char *before, const char *word)
{
	static const char *examples[] = {":en", ":en-US", ":english", ":united_kingdom"};
	char *value = lower(&p->a, word, '_', '-'), *tag, *name, *country, *english, *spaced, *c;
	const struct locale *l;
	size_t i, n = 0, len = strlen(value);

	if (!len) {
		for (i = 0; i < LEN(examples); i++)
			n = add(p, v, n, before, examples[i], NULL);
		return n;
	}
	spaced = lower(&p->a, value, '-', ' ');
	for (i = 1; i < nlocales; i++) {
		l = &locales[i];
		tag = lower(&p->a, l->tag, 0, 0);
		name = lower(&p->a, l->names, 0, 0);
		country = strchr(name, '|');
		*country++ = 0;
		english = strchr(country, '|');
		*english++ = 0;
		if (!strncmp(tag, value, len))
			n = add(p, v, n, before,
			    len <= 2 ? aprintf(&p->a, ":%.*s", (int)strcspn(l->tag, "-"), l->tag) :
			               aprintf(&p->a, ":%s", l->tag),
			    len <= 2 && strchr(l->tag, '-') ? NULL : l->label);
		if (*name && !strncmp(name, value, len))
			n = add(p, v, n, before, aprintf(&p->a, ":%s", name), NULL);
		if (*english && !strncmp(english, value, len))
			n = add(p, v, n, before, aprintf(&p->a, ":%s", english), NULL);
		if (*country && !strncmp(country, spaced, strlen(spaced))) {
			for (c = country; *c; c++)
				if (*c == ' ')
					*c = '_';
			n = add(p, v, n, before, aprintf(&p->a, ":%s", country), l->label);
		}
	}
	return n;
}

static size_t
complete_external(struct pending *p, struct suggestion *v, const char *before, const char *word)
{
	size_t i;

	for (i = 0; i < nengines; i++)
		if (engines[i]->bang && !strcasecmp(engines[i]->shortcut, word))
			return add(p, v, 0, before, aprintf(&p->a, "!!%s", engines[i]->shortcut),
			    aprintf(&p->a, "Go to %s", engines[i]->website));
	return 0;
}

static size_t
complete_timeout(struct pending *p, struct suggestion *v, const char *before, const char *word)
{
	const char *c;
	long n;

	for (c = word; IS_DIGIT(*c); c++)
		;
	if (!*word || *c)
		return 0;
	n = parse_long(word, 0);
	if (n >= 100 && n < 10000)
		return add(p, v, 0, before, aprintf(&p->a, "<%s", word), aprintf(&p->a, "%ld ms timeout", n));
	n = n < 100 ? MIN(n, 10) : 10;
	return add(p, v, 0, before, aprintf(&p->a, "<%s", word),
	    aprintf(&p->a, "%ld second%s timeout", n, n == 1 ? "" : "s"));
}

static int
complete_local(struct pending *p)
{
	const char *q = p->query, *word = q + strlen(q);
	struct suggestion *v;
	size_t n;

	if (!*q || IS_SPACE(word[-1]))
		return 0;
	while (word > q && !IS_SPACE(word[-1]))
		word--;
	if (!special(word))
		return 0;
	v = amalloc(&p->a, MAX_LOCAL * sizeof(*v));
	if (word[0] == '!' && word[1] == '!')
		n = complete_external(p, v, astrndup(&p->a, q, word - q), word + 2);
	else if (word[0] == '!')
		n = complete_bang(p, v, astrndup(&p->a, q, word - q), word + 1);
	else if (word[0] == ':')
		n = complete_language(p, v, astrndup(&p->a, q, word - q), word + 1);
	else
		n = complete_timeout(p, v, astrndup(&p->a, q, word - q), word + 1);
	respond(p, v, n);
	return 1;
}

static const char *
split_query(struct pending *p)
{
	char *words = astrdup(&p->a, p->query), *w, *save;
	struct buf pre = {0}, rest = {0};
	const char *out;

	for (w = strtok_r(words, " \t\n\r", &save); w; w = strtok_r(NULL, " \t\n\r", &save)) {
		struct buf *b = special(w) ? &pre : &rest;
		if (b->len)
			buf_putc(b, ' ');
		buf_puts(b, w);
	}
	p->prefix = astrdup(&p->a, buf_str(&pre));
	out = astrdup(&p->a, buf_str(&rest));
	buf_free(&pre);
	buf_free(&rest);
	return out;
}

void
autocomplete(struct conn *c, struct request *r)
{
	struct pending *p = xcalloc(1, sizeof(*p));
	struct buf q = {0};
	struct filters f;
	const char *query;

	p->c = c;
	query = query_param(&p->a, r->query, "q");
	p->query = astrdup(&p->a, query ? query : "");
	p->json = starts_with(r->accept, "application/json");
	p->firefox = strstr(r->user_agent, "Firefox") != NULL;
	p->chromium = *r->sec_ch_ua && !strstr(r->sec_ch_ua, "Google Chrome") &&
	    !strstr(r->sec_ch_ua, "Microsoft Edge") && strstr(r->sec_ch_ua, "Chromium");
	p->origin = aprintf(&p->a, "%s://%s", r->proto, *r->host ? r->host : "localhost");
	if (complete_local(p))
		return;
	query = split_query(p);
	if (!*query && *p->prefix) {
		respond(p, NULL, 0);
		return;
	}

	form_add(&q, "q", query);
	form_add(&q, "client", "gws-wiz");
	filters_parse(&p->a, r, &f);
	form_add(&q, "hl", hl(&p->a, f.locale->tag));
	p->f = fetch_new(aprintf(&p->a, "https://www.google.com/complete/search?%s", buf_str(&q)));
	buf_free(&q);
	curl_easy_setopt(p->f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(p->f, (long)(search_timeout * 1e3));
	http_on_close(c, on_close, p);
	fetch_start(p->f, done, p);
}
