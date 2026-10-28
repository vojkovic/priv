#include "core/util.h"
#include <ctype.h>
#include <errno.h>
#include <libxml/HTMLparser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <wctype.h>

//
// Text helpers: utf-8, stripping, and turning html into plain text
//
// some of these copy python's behaviour on purpose (py_strip, py_title,
// text_normalize), so results read the same as searxng's.
//

long
parse_long(const char *s, long fallback)
{
	char *end;
	long v;

	if (!s || !*s)
		return fallback;
	v = strtol(s, &end, 10);
	return *end ? fallback : v;
}

int
starts_with(const char *s, const char *prefix)
{
	return s && !strncmp(s, prefix, strlen(prefix));
}

const char *
find_between(const char *s, const char *begin, const char *end, size_t *n)
{
	const char *p, *q;

	if (!s || !(p = strstr(s, begin)))
		return NULL;
	p += strlen(begin);
	if (!(q = strstr(p, end)))
		return NULL;
	*n = q - p;
	return p;
}

char *
str_strip(char *s)
{
	char *e;

	while (IS_SPACE(*s))
		s++;
	e = s + strlen(s);
	while (e > s && IS_SPACE(e[-1]))
		e--;
	*e = 0;
	return s;
}

int
utf8_decode(const char *s, uint32_t *cp)
{
	const unsigned char *p = (const unsigned char *)s;
	int n, i;

	if (p[0] < 0x80) {
		*cp = p[0];
		return 1;
	}
	if ((p[0] & 0xE0) == 0xC0) {
		*cp = p[0] & 0x1F;
		n = 2;
	} else if ((p[0] & 0xF0) == 0xE0) {
		*cp = p[0] & 0x0F;
		n = 3;
	} else if ((p[0] & 0xF8) == 0xF0) {
		*cp = p[0] & 0x07;
		n = 4;
	} else {
		*cp = 0xFFFD;
		return 1;
	}
	for (i = 1; i < n; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return 1;
		}
		*cp = *cp << 6 | (p[i] & 0x3F);
	}
	return n;
}

int
utf8_encode(char *out, uint32_t cp)
{
	if (cp < 0x80) {
		out[0] = cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = 0xC0 | cp >> 6;
		out[1] = 0x80 | (cp & 0x3F);
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = 0xE0 | cp >> 12;
		out[1] = 0x80 | (cp >> 6 & 0x3F);
		out[2] = 0x80 | (cp & 0x3F);
		return 3;
	}
	out[0] = 0xF0 | cp >> 18;
	out[1] = 0x80 | (cp >> 12 & 0x3F);
	out[2] = 0x80 | (cp >> 6 & 0x3F);
	out[3] = 0x80 | (cp & 0x3F);
	return 4;
}

size_t
utf8_len(const char *s)
{
	size_t n = 0;

	for (; *s; s++)
		if (((unsigned char)*s & 0xC0) != 0x80)
			n++;
	return n;
}

int
is_py_space(uint32_t cp)
{
	return (cp >= 9 && cp <= 13) || (cp >= 0x1C && cp <= 0x20) || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
	    (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
	    cp == 0x3000;
}

char *
text_split_join(struct arena *a, const char *s)
{
	char *out = amalloc(a, strlen(s) + 1), *w = out;
	uint32_t cp;
	int n, space = 0;

	while (*s) {
		n = utf8_decode(s, &cp);
		if (is_py_space(cp)) {
			space = 1;
		} else {
			if (space && w != out)
				*w++ = ' ';
			space = 0;
			memcpy(w, s, n);
			w += n;
		}
		s += n;
	}
	*w = 0;
	return out;
}

char *
py_strip(char *s)
{
	uint32_t cp;
	char *e, *last;
	int n;

	while (*s && (n = utf8_decode(s, &cp)) && is_py_space(cp))
		s += n;
	last = s;
	for (e = s; *e; e += n) {
		n = utf8_decode(e, &cp);
		if (!is_py_space(cp))
			last = e + n;
	}
	*last = 0;
	return s;
}

char *
text_normalize(struct arena *a, const char *s, size_t max)
{
	char *out, *w, *p, *sp;
	size_t i;

	if (!s)
		return "";
	out = amalloc(a, strlen(s) + 8);
	for (w = out; *s; s++) {
		if (*s == ' ' || *s == '\t' || *s == '\n') {
			*w++ = ' ';
			while (s[1] == ' ' || s[1] == '\t' || s[1] == '\n')
				s++;
		} else
			*w++ = *s;
	}
	*w = 0;
	out = py_strip(out);
	if (utf8_len(out) <= max)
		return out;
	for (p = out, i = 0; *p && i < max; i++)
		p += utf8_decode(p, &(uint32_t){0});
	*p = 0;
	if ((sp = strrchr(out, ' ')))
		*sp = 0;
	memcpy(out + strlen(out), " \xe2\x80\xa6", 5);
	return out;
}

static void
put_entity(struct buf *b, const char *s, size_t n)
{
	const htmlEntityDesc *e;
	char name[32], tmp[4];
	uint32_t cp = 0;
	char *end;
	int nb;

	if (n == 0 || n >= sizeof(name))
		goto literal;
	memcpy(name, s, n);
	name[n] = 0;
	if (name[0] == '#') {
		if (name[1] == 'x' || name[1] == 'X')
			cp = strtoul(name + 2, &end, 16);
		else
			cp = strtoul(name + 1, &end, 10);
		if (*end || cp == 0 || cp > 0x10FFFF)
			goto literal;
	} else if ((e = htmlEntityLookup((const xmlChar *)name))) {
		cp = e->value;
	} else if (!strcmp(name, "apos")) {
		cp = '\'';
	} else
		goto literal;
	nb = utf8_encode(tmp, cp);
	buf_add(b, tmp, nb);
	return;
literal:
	buf_putc(b, '&');
	buf_add(b, s, n);
	buf_putc(b, ';');
}

static void
put_data(struct buf *b, const char *s, size_t n)
{
	const char *end = s + n, *amp, *semi;

	while (s < end) {
		amp = memchr(s, '&', end - s);
		if (!amp) {
			buf_add(b, s, end - s);
			return;
		}
		buf_add(b, s, amp - s);
		for (semi = amp + 1; semi < end && semi - amp < 32 && (IS_ALNUM(*semi) || *semi == '#'); semi++)
			;
		if (semi < end && *semi == ';') {
			put_entity(b, amp + 1, semi - amp - 1);
			s = semi + 1;
		} else {
			buf_putc(b, '&');
			s = amp + 1;
		}
	}
}

char *
html_to_text(struct arena *a, const char *s)
{
	struct buf b = {0};
	char tags[64][16], name[16], *r;
	const char *p, *gt, *end;
	size_t ntags = 0, n;
	int closing, blocked;

	if (!s || !*s)
		return "";
	s = text_split_join(a, s);
	for (p = s; *p;) {
		blocked = ntags && (!strcmp(tags[ntags - 1], "script") || !strcmp(tags[ntags - 1], "style"));
		if (*p != '<' || !(IS_ALPHA(p[1]) || p[1] == '/' || p[1] == '!')) {
			end = strchr(p + 1, '<');
			if (!end)
				end = p + strlen(p);
			if (!blocked)
				put_data(&b, p, end - p);
			p = end;
			continue;
		}
		if (!strncmp(p, "<!--", 4)) {
			end = strstr(p + 4, "-->");
			p = end ? end + 3 : p + strlen(p);
			continue;
		}
		if (!(gt = strchr(p, '>'))) {
			if (!blocked)
				buf_puts(&b, p);
			break;
		}
		closing = p[1] == '/';
		if (p[1] == '!') {
			p = gt + 1;
			continue;
		}
		const char *nm = p + 1 + closing;
		for (n = 0; n < sizeof(name) - 1 && (IS_ALNUM(nm[n]) || nm[n] == '-'); n++)
			name[n] = tolower((unsigned char)nm[n]);
		name[n] = 0;
		if (closing) {
			if (ntags && !strcmp(tags[ntags - 1], name))
				ntags--;
			else if (ntags)
				buf_printf(&b, "</%s>", name);
		} else {
			if (ntags < LEN(tags))
				snprintf(tags[ntags++], sizeof(tags[0]), "%s", name);
			if (!strcmp(name, "br"))
				buf_putc(&b, ' ');
			if (gt > p && gt[-1] == '/')
				ntags--;
		}
		p = gt + 1;
	}
	r = py_strip(astrdup(a, buf_str(&b)));
	buf_free(&b);
	return r;
}

char *
py_title(struct arena *a, const char *s)
{
	struct buf b = {0};
	uint32_t cp;
	int n, nb, prev_cased = 0;
	char tmp[4], *r;

	while (*s) {
		n = utf8_decode(s, &cp);
		if (iswalpha(cp)) {
			cp = prev_cased ? (uint32_t)towlower(cp) : (uint32_t)towupper(cp);
			prev_cased = 1;
		} else
			prev_cased = 0;
		nb = utf8_encode(tmp, cp);
		buf_add(&b, tmp, nb);
		s += n;
	}
	r = astrdup(a, buf_str(&b));
	buf_free(&b);
	return r;
}

int
py_islower(const char *s)
{
	uint32_t cp;
	int lower = 0;

	while (*s) {
		s += utf8_decode(s, &cp);
		if (iswupper(cp))
			return 0;
		if (iswlower(cp))
			lower = 1;
	}
	return lower;
}

char *
base64(struct arena *a, const unsigned char *p, size_t n)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *r = amalloc(a, (n + 2) / 3 * 4 + 1), *w = r;
	size_t i;

	for (i = 0; i + 2 < n; i += 3) {
		*w++ = t[p[i] >> 2];
		*w++ = t[(p[i] & 3) << 4 | p[i + 1] >> 4];
		*w++ = t[(p[i + 1] & 15) << 2 | p[i + 2] >> 6];
		*w++ = t[p[i + 2] & 63];
	}
	if (i < n) {
		*w++ = t[p[i] >> 2];
		*w++ = t[(p[i] & 3) << 4 | (i + 1 < n ? p[i + 1] >> 4 : 0)];
		*w++ = i + 1 < n ? t[(p[i + 1] & 15) << 2] : '=';
		*w++ = '=';
	}
	*w = 0;
	return r;
}
