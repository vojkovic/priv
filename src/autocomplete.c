#include <ctype.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../lib/yyjson/yyjson.h"
#include "autocomplete.h"
#include "engine.h"
#include "fetch.h"
#include "js.h"
#include "locales.h"
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
	const struct backend *backend;
	int json, firefox, chromium;
};

/* a suggestion service: the url to ask, with the query in its %s, and how to read the answer */
struct backend {
	const char *name, *url;
	const char *(*params)(struct pending *p, const char *query, const struct locale *l);
	size_t (*parse)(struct pending *p, yyjson_val *root, struct suggestion *v);
	int gbk;
};

#define MAX_LOCAL 12
#define MAX_REMOTE 15

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

/* adds one remote suggestion, copying it out of the parsed document */
static size_t
suggest(struct pending *p, struct suggestion *v, size_t n, const char *text, const char *icon, const char *description)
{
	if (n == MAX_REMOTE || !text || !*(text = html_to_text(&p->a, text)))
		return n;
	memset(&v[n], 0, sizeof(*v));
	v[n].text = (char *)text;
	v[n].icon = icon && *icon ? normalize_icon(&p->a, icon) : NULL;
	v[n].description = description && *description ? html_to_text(&p->a, description) : NULL;
	return n + 1;
}

static const char *
str(yyjson_val *v)
{
	return yyjson_get_str(v);
}

static const char *
key(yyjson_val *o, const char *k)
{
	return yyjson_get_str(yyjson_obj_get(o, k));
}

/* google: [[[text, 0, [], {zs: image, zi: description, zp: {gs_ss: trending}}], ...]] */
static size_t
parse_google(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item, *meta, *zp;
	size_t i, max, n = 0;
	const char *s;

	yyjson_arr_foreach (yyjson_arr_get(root, 0), i, max, item) {
		meta = yyjson_arr_get(item, 3);
		if (!yyjson_is_obj(meta)) {
			n = suggest(p, v, n, str(yyjson_arr_get(item, 0)), NULL, NULL);
			continue;
		}
		n = suggest(p, v, n, str(yyjson_arr_get(item, 0)), key(meta, "zs"), key(meta, "zi"));
		zp = yyjson_obj_get(meta, "zp");
		if (n && yyjson_is_obj(zp) && (s = key(zp, "gs_ss")) && !strcmp(s, "1"))
			v[n - 1].trending = 1;
	}
	return n;
}

/* the opensearch format: [query, [text, ...]] */
static size_t
parse_opensearch(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;
	const char *s;

	yyjson_arr_foreach (yyjson_arr_get(root, 1), i, max, item)
		if ((s = str(item)) && !starts_with(s, "go: ") && !starts_with(s, "search: "))
			n = suggest(p, v, n, s, NULL, NULL);
	return n;
}

/* a plain [text, ...] */
static size_t
parse_list(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;

	yyjson_arr_foreach (root, i, max, item)
		n = suggest(p, v, n, str(item), NULL, NULL);
	return n;
}

/* [{field: text}, ...] somewhere in the answer, found by the path before it */
static size_t
parse_field(struct pending *p, yyjson_val *list, const char *field, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;

	yyjson_arr_foreach (list, i, max, item)
		n = suggest(p, v, n, key(item, field), NULL, NULL);
	return n;
}

static size_t
parse_duckduckgo(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	return parse_field(p, root, "phrase", v);
}

static size_t
parse_baidu(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	return parse_field(p, yyjson_obj_get(root, "g"), "q", v);
}

static size_t
parse_360(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	return parse_field(p, yyjson_obj_get(root, "result"), "word", v);
}

static size_t
parse_quark(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	return parse_field(p, yyjson_obj_get(root, "r"), "w", v);
}

static size_t
parse_qwant(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	return parse_field(p, yyjson_obj_get(yyjson_obj_get(root, "data"), "items"), "value", v);
}

/* naver: {items: [[[text], ...]]} */
static size_t
parse_naver(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;

	yyjson_arr_foreach (yyjson_arr_get(yyjson_obj_get(root, "items"), 0), i, max, item)
		n = suggest(p, v, n, str(yyjson_arr_get(item, 0)), NULL, NULL);
	return n;
}

/* brave with rich=true: [query, [{q, is_entity, desc, img}, ...]] */
static size_t
parse_brave(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;
	int entity;

	yyjson_arr_foreach (yyjson_arr_get(root, 1), i, max, item) {
		entity = yyjson_get_bool(yyjson_obj_get(item, "is_entity"));
		n = suggest(p, v, n, key(item, "q"), entity ? key(item, "img") : NULL,
		    entity ? key(item, "desc") : NULL);
	}
	return n;
}

/* bing: {s: [{q, ext: {des, im}}]}, with U+E000 and U+E001 around the typed part */
static size_t
parse_bing(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item, *ext;
	size_t i, max, n = 0;
	const char *q, *im;
	char *t, *w, *r;

	yyjson_arr_foreach (yyjson_obj_get(root, "s"), i, max, item) {
		if (!(q = key(item, "q")))
			continue;
		t = astrdup(&p->a, q);
		for (r = w = t; *r;)
			if (!strncmp(r, "\xee\x80\x80", 3) || !strncmp(r, "\xee\x80\x81", 3))
				r += 3;
			else
				*w++ = *r++;
		*w = 0;
		ext = yyjson_obj_get(item, "ext");
		im = key(ext, "im");
		if (im && *im == '/' && im[1] != '/')
			im = aprintf(&p->a, "https://www.bing.com%s", im);
		n = suggest(p, v, n, t, im, key(ext, "des"));
	}
	return n;
}

/* wikipedia's title search: {pages: [{title, description, thumbnail: {url}}]} */
static size_t
parse_wikipedia(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;

	yyjson_arr_foreach (yyjson_obj_get(root, "pages"), i, max, item)
		n = suggest(p, v, n, key(item, "title"), key(yyjson_obj_get(item, "thumbnail"), "url"),
		    key(item, "description"));
	return n;
}

/* seznam: {result: [{itemType, text: [{text}], leadingMetaData: {type, value}, bottomMetaData: {type, value}}]} */
static size_t
parse_seznam(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item, *part, *lead, *bottom;
	size_t i, max, k, kmax, n = 0;
	struct buf t = {0};
	const char *s, *icon, *description;

	yyjson_arr_foreach (yyjson_obj_get(root, "result"), i, max, item) {
		if (!(s = key(item, "itemType")) || strcmp(s, "ItemType.TEXT"))
			continue;
		t.len = 0;
		yyjson_arr_foreach (yyjson_obj_get(item, "text"), k, kmax, part)
			if ((s = key(part, "text")))
				buf_puts(&t, s);
		lead = yyjson_obj_get(item, "leadingMetaData");
		bottom = yyjson_obj_get(item, "bottomMetaData");
		icon = (s = key(lead, "type")) && !strcmp(s, "MetaDataType.GRAPHICS") ? key(lead, "value") : NULL;
		description =
		    (s = key(bottom, "type")) && !strcmp(s, "MetaDataType.TEXT") ? key(bottom, "value") : NULL;
		n = suggest(p, v, n, buf_str(&t), icon, description);
	}
	buf_free(&t);
	return n;
}

/* dbpedia lookup: {docs: [{label: [name], typeName: [kind]}]}, names highlighted with <B> */
static size_t
parse_dbpedia(struct pending *p, yyjson_val *root, struct suggestion *v)
{
	yyjson_val *item;
	size_t i, max, n = 0;

	yyjson_arr_foreach (yyjson_obj_get(root, "docs"), i, max, item)
		n = suggest(p, v, n, str(yyjson_arr_get(yyjson_obj_get(item, "label"), 0)), NULL,
		    str(yyjson_arr_get(yyjson_obj_get(item, "typeName"), 0)));
	return n;
}

/* sogou answers in gbk */
static char *
from_gbk(struct arena *a, const char *s, size_t len)
{
	iconv_t cd = iconv_open("UTF-8", "GBK");
	size_t left = len, room = len * 2 + 1;
	char *out, *in = (char *)s, *w;

	if (cd == (iconv_t)-1) // NOLINT(performance-no-int-to-ptr)
		return NULL;
	out = w = amalloc(a, room);
	while (left && iconv(cd, &in, &left, &w, &room) == (size_t)-1 && left) {
		in++;
		left--;
	}
	*w = 0;
	iconv_close(cd);
	return out;
}

/* finds the json in an answer, which some services wrap in a callback or prefix with junk */
static size_t
parse(struct pending *p, struct fetch *f, struct suggestion **out)
{
	const char *body = fetch_body(f), *start;
	size_t end, len = f->body.len;
	yyjson_doc *d;
	size_t n;

	*out = NULL;
	if (p->backend->gbk && !(body = from_gbk(&p->a, body, len)))
		return 0;
	start = body + strcspn(body, "[{");
	if (!*start || (end = js_match(start, strlen(start), 0, *start, *start == '[' ? ']' : '}')) == JS_NOMATCH)
		return 0;
	if (!(d = yyjson_read(start, end + 1, YYJSON_READ_ALLOW_INVALID_UNICODE)))
		return 0;
	*out = amalloc(&p->a, MAX_REMOTE * sizeof(**out));
	n = p->backend->parse(p, yyjson_doc_get_root(d), *out);
	yyjson_doc_free(d);
	return n;
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
	r = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
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
	r = yyjson_mut_write(d, YYJSON_WRITE_ALLOW_INVALID_UNICODE, NULL);
	yyjson_mut_doc_free(d);
	return r;
}

static void
respond(struct pending *p, struct suggestion *v, size_t n)
{
	char *body = p->json ? render_json(p, v, n) : render_omnibox(p, v, n);

	http_respond(p->c, 200,
	    p->json ?
	        "Content-Type: application/json\r\nCache-Control: no-store\r\nVary: Accept, User-Agent, Cookie\r\n" :
	        "Content-Type: application/x-suggestions+json\r\n"
	        "Cache-Control: private, max-age=3600, stale-while-revalidate=300\r\nVary: Accept, User-Agent, Cookie\r\n",
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
		n = parse(p, f, &v);
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
	char *value = lower(&p->a, word, '_', ' ');
	struct engine *e;
	size_t i, n = 0, len;

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
region(const struct locale *l)
{
	const char *dash = strchr(l->tag, '-');

	return dash ? dash + 1 : NULL;
}

static const char *
params_google(struct pending *p, const char *query, const struct locale *l)
{
	return aprintf(&p->a, "&hl=%s", hl(&p->a, l->tag));
}

static const char *
params_bing(struct pending *p, const char *query, const struct locale *l)
{
	static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	char cvid[33];
	size_t i;

	for (i = 0; i < 32; i++)
		cvid[i] = chars[rnd(sizeof(chars) - 1)];
	cvid[32] = 0;
	return aprintf(&p->a, "&cvid=%s", cvid);
}

static const char *
params_duckduckgo(struct pending *p, const char *query, const struct locale *l)
{
	return *l->ddg_region ? aprintf(&p->a, "&kl=%s", l->ddg_region) : "";
}

static const char *
params_kagi(struct pending *p, const char *query, const struct locale *l)
{
	return region(l) ? lower(&p->a, aprintf(&p->a, "&r=%s", region(l)), 0, 0) : "";
}

static const char *
params_privacywall(struct pending *p, const char *query, const struct locale *l)
{
	return *l->privacywall_cc ? aprintf(&p->a, "&cc=%s", l->privacywall_cc) : "";
}

static const char *
params_qwant(struct pending *p, const char *query, const struct locale *l)
{
	const char *r = region(l);

	return r ? aprintf(&p->a, "&locale=%.*s_%s", (int)(r - 1 - l->tag), l->tag, r) : "&locale=en_US";
}

static const char *
params_seznam(struct pending *p, const char *query, const struct locale *l)
{
	return aprintf(&p->a, "&cursorPosition=%zu", strlen(query));
}

static const char *
params_wikipedia(struct pending *p, const char *query, const struct locale *l)
{
	return *l->wiki_netloc ? l->wiki_netloc : "en.wikipedia.org";
}

static const struct backend backends[] = {
    {"google", "https://www.google.com/complete/search?client=gws-wiz&q=%s", params_google, parse_google, 0},
    {"360search", "https://sug.so.360.cn/suggest?format=json&word=%s", NULL, parse_360, 0},
    {"baidu", "https://www.baidu.com/sugrec?ie=utf-8&json=1&prod=pc&wd=%s", NULL, parse_baidu, 0},
    {"bing", "https://www.bing.com/AS/Suggestions?csr=1&qry=%s", params_bing, parse_bing, 0},
    {"brave", "https://search.brave.com/api/suggest?rich=true&source=web&q=%s", NULL, parse_brave, 0},
    {"dbpedia", "https://lookup.dbpedia.org/api/search?format=json&maxResults=10&query=%s", NULL, parse_dbpedia, 0},
    {"duckduckgo", "https://duckduckgo.com/ac/?q=%s", params_duckduckgo, parse_duckduckgo, 0},
    {"kagi", "https://kagisuggest.com/api/autosuggest?q=%s", params_kagi, parse_opensearch, 0},
    {"mwmbl", "https://api.mwmbl.org/search/complete?q=%s", NULL, parse_opensearch, 0},
    {"naver", "https://ac.search.naver.com/nx/ac?r_format=json&st=0&q=%s", NULL, parse_naver, 0},
    {"privacywall", "https://www.privacywall.org/search/secure/suggestions.php?q=%s", params_privacywall,
        parse_opensearch, 0},
    {"quark", "https://sugs.m.sm.cn/web?q=%s", NULL, parse_quark, 0},
    {"qwant", "https://api.qwant.com/v3/suggest?version=2&q=%s", params_qwant, parse_qwant, 0},
    {"seznam", "https://suggest.seznam.cz/fulltext/cs?format=json-2&highlight=1&count=6&phrase=%s", params_seznam,
        parse_seznam, 0},
    {"sogou", "https://sor.html5.qq.com/api/getsug?m=privy&key=%s", NULL, parse_opensearch, 1},
    {"startpage", "https://www.startpage.com/osuggestions?q=%s", NULL, parse_opensearch, 0},
    {"swisscows", "https://api.swisscows.com/suggest?itemsCount=5&query=%s", NULL, parse_list, 0},
    {"wikipedia", "https://%s/w/rest.php/v1/search/title?limit=10&q=%s", params_wikipedia, parse_wikipedia, 0},
    {"yandex", "https://suggest.yandex.com/suggest-ff.cgi?part=%s", NULL, parse_opensearch, 0},
};

const struct autocompleter autocompleters[] = {
    {"google", "Google"},
    {"360search", "360search"},
    {"baidu", "Baidu"},
    {"bing", "Bing"},
    {"brave", "Brave"},
    {"dbpedia", "DBpedia"},
    {"duckduckgo", "DuckDuckGo"},
    {"kagi", "Kagi"},
    {"mwmbl", "Mwmbl"},
    {"naver", "Naver"},
    {"privacywall", "PrivacyWall"},
    {"quark", "Quark"},
    {"qwant", "Qwant"},
    {"seznam", "Seznam"},
    {"sogou", "Sogou"},
    {"startpage", "Startpage"},
    {"swisscows", "Swisscows"},
    {"wikipedia", "Wikipedia"},
    {"yandex", "Yandex"},
    {"off", "Off"},
};
const size_t nautocompleters = LEN(autocompleters);

static const struct backend *
backend_find(const char *name)
{
	size_t i;

	for (i = 0; i < LEN(backends); i++)
		if (!strcmp(backends[i].name, name))
			return &backends[i];
	return NULL;
}

static const char *
request_url(struct pending *p, const char *query, const struct locale *l)
{
	const struct backend *b = p->backend;
	const char *extra = b->params ? b->params(p, query, l) : "";
	struct buf q = {0};
	char *r;

	url_quote(&q, query, "", 1);
	/* wikipedia's host is its parameter */
	if (b->parse == parse_wikipedia)
		r = aprintf(&p->a, b->url, extra, buf_str(&q));
	else
		r = aprintf(&p->a, "%s%s", aprintf(&p->a, b->url, buf_str(&q)), extra);
	buf_free(&q);
	return r;
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

	filters_parse(&p->a, r, &f);
	if (!(p->backend = backend_find(f.prefs.autocomplete))) {
		respond(p, NULL, 0);
		return;
	}
	/* an empty box shows what is trending, which only google has */
	if (!*query && p->backend->parse != parse_google) {
		respond(p, NULL, 0);
		return;
	}
	p->f = fetch_new(request_url(p, query, f.locale));
	curl_easy_setopt(p->f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(p->f, (long)(search_timeout * 1e3));
	http_on_close(c, on_close, p);
	fetch_start(p->f, done, p);
}
