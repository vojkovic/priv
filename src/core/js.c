#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "core/js.h"
#include "core/util.h"

struct p {
	yyjson_mut_doc *d;
	const char *s;
	size_t len, i;
	const struct js_env *env;
	int depth;
};

size_t
js_match(const char *s, size_t len, size_t i, char open, char close)
{
	int depth = 0, esc = 0;
	char str = 0;

	for (; i < len; i++) {
		char c = s[i];
		if (str) {
			if (esc)
				esc = 0;
			else if (c == '\\')
				esc = 1;
			else if (c == str)
				str = 0;
		} else if (c == '"' || c == '\'' || c == '`')
			str = c;
		else if (c == open)
			depth++;
		else if (c == close && --depth == 0)
			return i;
	}
	return JS_NOMATCH;
}

static void
ws(struct p *p)
{
	while (p->i < p->len && IS_SPACE(p->s[p->i]))
		p->i++;
}

static int
peek(struct p *p)
{
	ws(p);
	return p->i < p->len ? (unsigned char)p->s[p->i] : -1;
}

static int
is_ident(int c, int first)
{
	return IS_ALPHA(c) || c == '_' || c == '$' || (!first && IS_DIGIT(c)) || c >= 0x80;
}

static int
hexn(const char *s, size_t n, uint32_t *out)
{
	uint32_t v = 0;
	size_t i;

	for (i = 0; i < n; i++) {
		int c = tolower((unsigned char)s[i]);
		if (IS_DIGIT(c))
			v = v * 16 + c - '0';
		else if (c >= 'a' && c <= 'f')
			v = v * 16 + c - 'a' + 10;
		else
			return -1;
	}
	*out = v;
	return 0;
}

static char *
string(struct p *p, size_t *outlen)
{
	struct buf b = {0};
	char q = p->s[p->i++], tmp[4], *r;
	uint32_t cp, lo;
	int nb;

	while (p->i < p->len && p->s[p->i] != q) {
		char c = p->s[p->i++];
		if (c != '\\') {
			buf_putc(&b, c);
			continue;
		}
		if (p->i >= p->len)
			break;
		c = p->s[p->i++];
		switch (c) {
		case 'n': buf_putc(&b, '\n'); break;
		case 't': buf_putc(&b, '\t'); break;
		case 'r': buf_putc(&b, '\r'); break;
		case 'b': buf_putc(&b, '\b'); break;
		case 'f': buf_putc(&b, '\f'); break;
		case 'v': buf_putc(&b, '\v'); break;
		case '0': buf_putc(&b, '\0'); break;
		case '\n': break;
		case 'x':
			if (p->i + 2 <= p->len && !hexn(p->s + p->i, 2, &cp)) {
				p->i += 2;
				nb = utf8_encode(tmp, cp);
				buf_add(&b, tmp, nb);
			}
			break;
		case 'u':
			if (p->i < p->len && p->s[p->i] == '{') {
				const char *e = memchr(p->s + p->i, '}', p->len - p->i);
				if (e && !hexn(p->s + p->i + 1, e - p->s - p->i - 1, &cp)) {
					p->i = e - p->s + 1;
					nb = utf8_encode(tmp, cp);
					buf_add(&b, tmp, nb);
				}
			} else if (p->i + 4 <= p->len && !hexn(p->s + p->i, 4, &cp)) {
				p->i += 4;
				if (cp >= 0xD800 && cp < 0xDC00 && p->i + 6 <= p->len && p->s[p->i] == '\\' &&
				    p->s[p->i + 1] == 'u' && !hexn(p->s + p->i + 2, 4, &lo) && lo >= 0xDC00 &&
				    lo < 0xE000) {
					p->i += 6;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				nb = utf8_encode(tmp, cp);
				buf_add(&b, tmp, nb);
			}
			break;
		default: buf_putc(&b, c);
		}
	}
	p->i++;
	*outlen = b.len;
	r = b.p ? b.p : xstrdup("");
	return r;
}

static yyjson_mut_val *value(struct p *p);

static yyjson_mut_val *
ident(struct p *p)
{
	size_t start = p->i, n, k;

	while (p->i < p->len && is_ident((unsigned char)p->s[p->i], p->i == start))
		p->i++;
	n = p->i - start;
	const char *w = p->s + start;
#define IS(lit) (n == sizeof(lit) - 1 && !memcmp(w, lit, n))
	if (IS("true"))
		return yyjson_mut_true(p->d);
	if (IS("false"))
		return yyjson_mut_false(p->d);
	if (IS("null") || IS("undefined") || IS("NaN"))
		return yyjson_mut_null(p->d);
	if (IS("Infinity"))
		return yyjson_mut_real(p->d, 1e308);
	if (IS("void")) {
		if (peek(p) == '(') {
			size_t e = js_match(p->s, p->len, p->i, '(', ')');
			p->i = e == JS_NOMATCH ? p->len : e + 1;
		} else
			value(p);
		return yyjson_mut_null(p->d);
	}
#undef IS
	if (p->env)
		for (k = 0; k < p->env->n; k++)
			if (p->env->lens[k] == n && !memcmp(p->env->names[k], w, n))
				return yyjson_mut_val_mut_copy(p->d, p->env->vals[k]);
	return n ? yyjson_mut_null(p->d) : NULL;
}

static yyjson_mut_val *
number(struct p *p)
{
	char tmp[64], *end;
	size_t n = 0;
	double d;

	while (p->i < p->len && n < sizeof(tmp) - 1 && (IS_ALNUM(p->s[p->i]) || strchr("+-.", p->s[p->i])))
		tmp[n++] = p->s[p->i++];
	tmp[n] = 0;
	d = strtod(tmp, &end);
	if (end == tmp)
		return NULL;
	if (!strpbrk(tmp, ".eE") && d >= -9e18 && d <= 9e18)
		return yyjson_mut_sint(p->d, (int64_t)d);
	return yyjson_mut_real(p->d, d);
}

static yyjson_mut_val *
key(struct p *p)
{
	size_t start, n;
	char *s;
	int c = peek(p);

	if (c == '"' || c == '\'' || c == '`') {
		s = string(p, &n);
		yyjson_mut_val *v = yyjson_mut_strncpy(p->d, s, n);
		free(s);
		return v;
	}
	start = p->i;
	while (p->i < p->len && (is_ident((unsigned char)p->s[p->i], 0) || p->s[p->i] == '.' || p->s[p->i] == '-'))
		p->i++;
	if (p->i == start)
		return NULL;
	return yyjson_mut_strncpy(p->d, p->s + start, p->i - start);
}

static yyjson_mut_val *
value(struct p *p)
{
	yyjson_mut_val *v, *k, *e;
	size_t n;
	char *s;
	int c = peek(p);

	if (c < 0 || ++p->depth > 512)
		return NULL;
	switch (c) {
	case '{':
		p->i++;
		v = yyjson_mut_obj(p->d);
		while ((c = peek(p)) != '}') {
			if (c < 0 || !(k = key(p)) || peek(p) != ':')
				return NULL;
			p->i++;
			if (!(e = value(p)))
				return NULL;
			yyjson_mut_obj_add(v, k, e);
			if (peek(p) == ',')
				p->i++;
		}
		p->i++;
		break;
	case '[':
		p->i++;
		v = yyjson_mut_arr(p->d);
		while ((c = peek(p)) != ']') {
			if (c < 0 || !(e = value(p)))
				return NULL;
			yyjson_mut_arr_append(v, e);
			if (peek(p) == ',')
				p->i++;
		}
		p->i++;
		break;
	case '"':
	case '\'':
	case '`':
		s = string(p, &n);
		v = yyjson_mut_strncpy(p->d, s, n);
		free(s);
		break;
	case '!':
		p->i++;
		e = value(p);
		v = yyjson_mut_bool(p->d,
		    !e || yyjson_mut_is_null(e) || yyjson_mut_is_false(e) ||
		        (yyjson_mut_is_num(e) && yyjson_mut_get_num(e) == 0));
		break;
	default:
		if (IS_DIGIT(c) || c == '-' || c == '+' || c == '.')
			v = number(p);
		else if (is_ident(c, 1))
			v = ident(p);
		else
			v = NULL;
	}
	p->depth--;
	return v;
}

yyjson_mut_val *
js_parse(yyjson_mut_doc *d, const char *s, size_t len, size_t *pos, const struct js_env *env)
{
	struct p p = {d, s, len, pos ? *pos : 0, env, 0};
	yyjson_mut_val *v = value(&p);

	if (pos)
		*pos = p.i;
	return v;
}

#define MAX_PARAMS 64

static size_t
skip_spaces(const char *s, size_t n, size_t i)
{
	while (i < n && IS_SPACE(s[i]))
		i++;
	return i;
}

static size_t
literal(const char *s, size_t n, size_t i, const char *w)
{
	size_t l = strlen(w);

	return i + l <= n && !memcmp(s + i, w, l) ? i + l : JS_NOMATCH;
}

/* (function(a,b,...){return {   -> index of the body's '{' */
static size_t
match_iife(const char *s, size_t n, size_t i, size_t *pstart, size_t *pend)
{
	if ((i = literal(s, n, i, "(function")) == JS_NOMATCH)
		return JS_NOMATCH;
	i = skip_spaces(s, n, i);
	if ((i = literal(s, n, i, "(")) == JS_NOMATCH)
		return JS_NOMATCH;
	*pstart = i;
	while (i < n && s[i] != ')')
		i++;
	*pend = i;
	if ((i = literal(s, n, i, ")")) == JS_NOMATCH)
		return JS_NOMATCH;
	i = skip_spaces(s, n, i);
	if ((i = literal(s, n, i, "{")) == JS_NOMATCH)
		return JS_NOMATCH;
	i = skip_spaces(s, n, i);
	if ((i = literal(s, n, i, "return")) == JS_NOMATCH)
		return JS_NOMATCH;
	i = skip_spaces(s, n, i);
	return i < n && s[i] == '{' ? i : JS_NOMATCH;
}

static yyjson_mut_val *
iife(yyjson_mut_doc *d, const char *s, size_t n, size_t body, size_t pstart, size_t pend, size_t *next)
{
	const char *names[MAX_PARAMS];
	size_t lens[MAX_PARAMS], np = 0, na = 0, i, body_end, call, call_end, p;
	yyjson_mut_val *vals[MAX_PARAMS], *v;
	struct js_env env;
	const char *cp;

	for (i = pstart; i < pend && np < MAX_PARAMS;) {
		while (i < pend && (IS_SPACE(s[i]) || s[i] == ','))
			i++;
		p = i;
		while (i < pend && s[i] != ',' && !IS_SPACE(s[i]))
			i++;
		if (i > p) {
			names[np] = s + p;
			lens[np++] = i - p;
		}
	}
	if ((body_end = js_match(s, n, body, '{', '}')) == JS_NOMATCH)
		return NULL;
	if (!(cp = memchr(s + body_end, '(', n - body_end)))
		return NULL;
	call = cp - s;
	if ((call_end = js_match(s, n, call, '(', ')')) == JS_NOMATCH)
		return NULL;
	for (p = call + 1; na < np;) {
		p = skip_spaces(s, call_end, p);
		if (p >= call_end)
			break;
		if (!(v = js_parse(d, s, call_end, &p, NULL)))
			return NULL;
		vals[na++] = v;
		p = skip_spaces(s, call_end, p);
		if (p < call_end && s[p] == ',')
			p++;
	}
	env.n = na;
	env.names = names;
	env.lens = lens;
	env.vals = vals;
	p = body;
	v = js_parse(d, s, body_end + 1, &p, &env);
	*next = call_end + 1;
	return v;
}

/* the data array of a SvelteKit page: kit.start(app, element, {... data: [...]}) */
yyjson_mut_val *
js_page_data(yyjson_mut_doc *d, const char *text)
{
	const char *start = strstr(text, "data: [");
	size_t n = strlen(text), as, ae, pos, body, ps, pe, next;
	yyjson_mut_val *arr, *v;

	if (!start)
		return NULL;
	as = start - text + 6;
	if ((ae = js_match(text, n, as, '[', ']')) == JS_NOMATCH)
		return NULL;
	arr = yyjson_mut_arr(d);
	for (pos = as + 1; pos < ae;) {
		while (pos < ae && strchr(" \t\n\r,", text[pos]))
			pos++;
		if (pos >= ae)
			break;
		if ((body = match_iife(text, ae, pos, &ps, &pe)) != JS_NOMATCH) {
			if (!(v = iife(d, text, ae, body, ps, pe, &next)))
				return NULL;
			pos = next;
			while (pos < ae && strchr(" \t\n\r)", text[pos]))
				pos++;
		} else if (text[pos] == '{') {
			if (!(v = js_parse(d, text, ae, &pos, NULL)))
				return NULL;
		} else
			return NULL;
		yyjson_mut_arr_append(arr, v);
	}
	return yyjson_mut_arr_size(arr) ? arr : NULL;
}
