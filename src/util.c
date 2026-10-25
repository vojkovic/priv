#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <wctype.h>

#include <libxml/HTMLparser.h>

#include "util.h"

_Noreturn void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void *
xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		die("out of memory");
	return p;
}

void *
xcalloc(size_t n, size_t m)
{
	void *p = calloc(n ? n : 1, m ? m : 1);

	if (!p)
		die("out of memory");
	return p;
}

void *
xrealloc(void *p, size_t n)
{
	void *r = realloc(p, n ? n : 1);

	if (!r)
		die("out of memory");
	return r;
}

char *
xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;

	return memcpy(xmalloc(n), s, n);
}

void
buf_grow(struct buf *b, size_t n)
{
	if (b->p && b->len + n + 1 <= b->cap)
		return;
	if (!b->cap)
		b->cap = 256;
	while (b->len + n + 1 > b->cap)
		b->cap *= 2;
	b->p = xrealloc(b->p, b->cap);
}

void
buf_add(struct buf *b, const void *p, size_t n)
{
	if (!n)
		return;
	buf_grow(b, n);
	memcpy(b->p + b->len, p, n);
	b->len += n;
	b->p[b->len] = 0;
}

void
buf_puts(struct buf *b, const char *s)
{
	buf_add(b, s, strlen(s));
}

void
buf_putc(struct buf *b, char c)
{
	buf_add(b, &c, 1);
}

void
buf_vprintf(struct buf *b, const char *fmt, va_list ap)
{
	va_list cp;
	int n;

	va_copy(cp, ap);
	n = vsnprintf(NULL, 0, fmt, cp);
	va_end(cp);
	if (n < 0)
		return;
	buf_grow(b, n);
	vsnprintf(b->p + b->len, n + 1, fmt, ap);
	b->len += n;
}

void
buf_printf(struct buf *b, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	buf_vprintf(b, fmt, ap);
	va_end(ap);
}

/* %s escaped, %r raw, %u form-encoded, %d int, %z size_t, %f seconds */
void
buf_html(struct buf *b, const char *fmt, ...)
{
	va_list ap;
	const char *s;

	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			s = strchr(fmt, '%');
			if (!s)
				s = fmt + strlen(fmt);
			buf_add(b, fmt, s - fmt);
			fmt = s - 1;
			continue;
		}
		switch (*++fmt) {
		case 's':
			s = va_arg(ap, const char *);
			html_escape(b, s ? s : "");
			break;
		case 'r':
			s = va_arg(ap, const char *);
			buf_puts(b, s ? s : "");
			break;
		case 'u':
			s = va_arg(ap, const char *);
			url_quote(b, s ? s : "", "", 1);
			break;
		case 'd': buf_printf(b, "%d", va_arg(ap, int)); break;
		case 'z': buf_printf(b, "%zu", va_arg(ap, size_t)); break;
		case 'f': buf_printf(b, "%.2f", va_arg(ap, double)); break;
		case '%': buf_putc(b, '%'); break;
		case 0: fmt--; break;
		default: break;
		}
	}
	va_end(ap);
}

const char *
buf_str(struct buf *b)
{
	return b->p ? b->p : "";
}

void
buf_free(struct buf *b)
{
	free(b->p);
	b->p = NULL;
	b->len = b->cap = 0;
}

struct chunk {
	struct chunk *next;
	size_t used, cap;
	_Alignas(16) unsigned char data[];
};

void *
amalloc(struct arena *a, size_t n)
{
	struct chunk *c = a->head;
	void *p;
	size_t cap;

	n = (n + 15) & ~(size_t)15;
	if (!c || c->used + n > c->cap) {
		cap = MAX(n, (size_t)65536 - sizeof(*c));
		c = xmalloc(sizeof(*c) + cap);
		c->cap = cap;
		c->used = 0;
		c->next = a->head;
		a->head = c;
	}
	p = c->data + c->used;
	c->used += n;
	return p;
}

char *
astrndup(struct arena *a, const char *s, size_t n)
{
	char *p = amalloc(a, n + 1);

	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

char *
astrdup(struct arena *a, const char *s)
{
	return s ? astrndup(a, s, strlen(s)) : NULL;
}

char *
aprintf(struct arena *a, const char *fmt, ...)
{
	va_list ap, cp;
	char *p;
	int n;

	va_start(ap, fmt);
	va_copy(cp, ap);
	n = vsnprintf(NULL, 0, fmt, cp);
	va_end(cp);
	p = amalloc(a, n + 1);
	vsnprintf(p, n + 1, fmt, ap);
	va_end(ap);
	return p;
}

void
arena_free(struct arena *a)
{
	struct chunk *c, *next;

	for (c = a->head; c; c = next) {
		next = c->next;
		free(c);
	}
	a->head = NULL;
}

double
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

double
wall_time(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

uint32_t
rnd(uint32_t n)
{
	return n ? (uint32_t)random() % n : 0;
}

static const char *
html_entity(char c)
{
	switch (c) {
	case '&': return "&amp;";
	case '<': return "&lt;";
	case '>': return "&gt;";
	case '"': return "&quot;";
	case '\'': return "&#39;";
	default: return NULL;
	}
}

void
html_escape(struct buf *b, const char *s)
{
	const char *p, *e;

	for (p = s; *p; p++) {
		if (!(e = html_entity(*p)))
			continue;
		buf_add(b, s, p - s);
		buf_puts(b, e);
		s = p + 1;
	}
	buf_add(b, s, p - s);
}

void
url_quote(struct buf *b, const char *s, const char *safe, int plus)
{
	static const char hex[] = "0123456789ABCDEF";
	unsigned char c;

	for (; (c = *s); s++) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("_.-~", c) ||
		    (safe[0] && strchr(safe, c)))
			buf_putc(b, c);
		else if (c == ' ' && plus)
			buf_putc(b, '+');
		else {
			buf_putc(b, '%');
			buf_putc(b, hex[c >> 4]);
			buf_putc(b, hex[c & 15]);
		}
	}
}

void
form_add(struct buf *b, const char *key, const char *val)
{
	if (b->len)
		buf_putc(b, '&');
	url_quote(b, key, "", 1);
	buf_putc(b, '=');
	url_quote(b, val ? val : "", "", 1);
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c = tolower(c);
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

char *
url_decode(char *s)
{
	char *r = s, *w = s;
	int h, l;

	for (; *r; r++) {
		if (*r == '+')
			*w++ = ' ';
		else if (*r == '%' && (h = hexval(r[1])) >= 0 && (l = hexval(r[2])) >= 0) {
			*w++ = h << 4 | l;
			r += 2;
		} else
			*w++ = *r;
	}
	*w = 0;
	return s;
}

const char *
query_param(struct arena *a, const char *qs, const char *key)
{
	const char *p, *end, *eq;
	char *k;

	for (p = qs; p && *p; p = *end ? end + 1 : end) {
		end = p + strcspn(p, "&");
		eq = memchr(p, '=', end - p);
		k = url_decode(astrndup(a, p, (eq ? eq : end) - p));
		if (!strcmp(k, key))
			return eq ? url_decode(astrndup(a, eq + 1, end - eq - 1)) : "";
	}
	return NULL;
}

static int
uses_params(const char *scheme)
{
	static const char *l[] = {"", "ftp", "hdl", "prospero", "http", "imap", "https", "shttp", "rtsp", "rtsps",
	    "rtspu", "sip", "sips", "mms", "sftp", "tel"};
	size_t i;

	for (i = 0; i < LEN(l); i++)
		if (!strcmp(scheme, l[i]))
			return 1;
	return 0;
}

int
url_parse(struct arena *a, const char *s, struct url *u)
{
	const char *p, *q;
	char *path, *semi, *slash;
	size_t n;

	memset(u, 0, sizeof(*u));
	u->scheme = "";
	p = strchr(s, ':');
	if (p && p > s && IS_ALPHA(*s)) {
		for (q = s; q < p && (IS_ALNUM(*q) || strchr("+-.", *q)); q++)
			;
		if (q == p) {
			u->scheme = astrndup(a, s, p - s);
			for (path = u->scheme; *path; path++)
				*path = tolower((unsigned char)*path);
			s = p + 1;
		}
	}
	u->netloc = "";
	if (s[0] == '/' && s[1] == '/') {
		n = strcspn(s + 2, "/?#");
		u->netloc = astrndup(a, s + 2, n);
		s += 2 + n;
	}
	u->fragment = "";
	if ((p = strchr(s, '#'))) {
		u->fragment = astrdup(a, p + 1);
	} else
		p = s + strlen(s);
	u->query = "";
	q = memchr(s, '?', p - s);
	if (q)
		u->query = astrndup(a, q + 1, p - q - 1);
	else
		q = p;
	path = astrndup(a, s, q - s);
	u->params = "";
	if (uses_params(u->scheme) && strchr(path, ';')) {
		slash = strrchr(path, '/');
		semi = strchr(slash ? slash : path, ';');
		if (semi) {
			u->params = semi + 1;
			*semi = 0;
		}
	}
	u->path = path;
	return 0;
}

char *
url_unparse(struct arena *a, const struct url *u)
{
	struct buf b = {0};
	char *r;

	if (u->scheme[0])
		buf_printf(&b, "%s:", u->scheme);
	if (u->netloc[0] || (u->scheme[0] && strncmp(u->path, "//", 2))) {
		buf_printf(&b, "//%s", u->netloc);
		if (u->path[0] && u->path[0] != '/')
			buf_putc(&b, '/');
	}
	buf_puts(&b, u->path);
	if (u->params[0])
		buf_printf(&b, ";%s", u->params);
	if (u->query[0])
		buf_printf(&b, "?%s", u->query);
	if (u->fragment[0])
		buf_printf(&b, "#%s", u->fragment);
	r = astrdup(a, buf_str(&b));
	buf_free(&b);
	return r;
}

char *
url_join(struct arena *a, const char *base, const char *ref)
{
	struct url u;
	const char *slash;

	if (!ref || !*ref)
		return astrdup(a, base);
	url_parse(a, ref, &u);
	if (u.scheme[0])
		return astrdup(a, ref);
	url_parse(a, base, &u);
	if (ref[0] == '/' && ref[1] == '/')
		return aprintf(a, "%s:%s", u.scheme, ref);
	if (ref[0] == '/')
		return aprintf(a, "%s://%s%s", u.scheme, u.netloc, ref);
	slash = strrchr(u.path, '/');
	return aprintf(a, "%s://%s%.*s/%s", u.scheme, u.netloc, slash ? (int)(slash - u.path) : 0, u.path, ref);
}

char *
url_query_get(struct arena *a, const char *url, const char *key)
{
	struct url u;

	url_parse(a, url, &u);
	return (char *)query_param(a, u.query, key);
}

static uint32_t
puny_adapt(uint32_t delta, uint32_t npoints, int first)
{
	uint32_t k = 0;

	delta = first ? delta / 700 : delta / 2;
	delta += delta / npoints;
	while (delta > 35 * 26 / 2) {
		delta /= 35;
		k += 36;
	}
	return k + 36 * delta / (delta + 38);
}

static int
puny_decode(const char *in, size_t len, struct buf *out)
{
	uint32_t cp[256], n = 128, i = 0, bias = 72, oldi, w, k, t, digit, nout = 0;
	size_t pos = 0, b, j;
	char tmp[4];
	int nb;

	for (b = len; b > 0 && in[b - 1] != '-'; b--)
		;
	if (b) {
		for (j = 0; j < b - 1; j++) {
			if (nout >= LEN(cp) || (unsigned char)in[j] >= 0x80)
				return -1;
			cp[nout++] = (unsigned char)in[j];
		}
		pos = b;
	}
	while (pos < len) {
		oldi = i;
		w = 1;
		for (k = 36;; k += 36) {
			if (pos >= len)
				return -1;
			digit = (unsigned char)in[pos++];
			if (digit >= '0' && digit <= '9')
				digit = digit - '0' + 26;
			else if (digit >= 'a' && digit <= 'z')
				digit -= 'a';
			else if (digit >= 'A' && digit <= 'Z')
				digit -= 'A';
			else
				return -1;
			i += digit * w;
			t = k <= bias ? 1 : k >= bias + 26 ? 26 : k - bias;
			if (digit < t)
				break;
			w *= 36 - t;
		}
		bias = puny_adapt(i - oldi, nout + 1, oldi == 0);
		n += i / (nout + 1);
		i %= nout + 1;
		if (nout >= LEN(cp) || n > 0x10FFFF)
			return -1;
		memmove(cp + i + 1, cp + i, (nout - i) * sizeof(*cp));
		cp[i++] = n;
		nout++;
	}
	for (j = 0; j < nout; j++) {
		nb = utf8_encode(tmp, cp[j]);
		buf_add(out, tmp, nb);
	}
	return 0;
}

char *
idna_decode(struct arena *a, const char *host)
{
	struct buf b = {0};
	const char *p = host, *dot;
	size_t n, mark;
	char *r;

	for (;;) {
		dot = strchr(p, '.');
		n = dot ? (size_t)(dot - p) : strlen(p);
		mark = b.len;
		if (n > 4 && !strncasecmp(p, "xn--", 4)) {
			if (puny_decode(p + 4, n - 4, &b) < 0) {
				b.len = mark;
				buf_add(&b, p, n);
			}
		} else
			buf_add(&b, p, n);
		if (!dot)
			break;
		buf_putc(&b, '.');
		p = dot + 1;
	}
	r = astrdup(a, buf_str(&b));
	buf_free(&b);
	return r;
}

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

/* the only links taken from upstream data, so none can run script when clicked */
int
web_url(const char *s)
{
	return s && (!strncasecmp(s, "https://", 8) || !strncasecmp(s, "http://", 7));
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
gen_useragent(void)
{
	size_t nos = 0, nver = 0;
	const char *os, *ver;
	char buf[256];

	while (useragent_os[nos])
		nos++;
	while (useragent_versions[nver])
		nver++;
	os = useragent_os[rnd(nos)];
	ver = useragent_versions[rnd(nver)];
	snprintf(buf, sizeof(buf), "Mozilla/5.0 (%s; rv:%s) Gecko/20100101 Firefox/%s", os, ver, ver);
	return xstrdup(buf);
}

struct centry {
	struct centry *next;
	char *key, *val;
	double expire;
};

static struct centry *ctable[1024];
static size_t ccount;

static uint32_t
strhash(const char *s)
{
	uint32_t h = 2166136261u;

	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

static void
cache_purge(void)
{
	struct centry **pp, *e;
	double now = now_ms();
	size_t i;

	for (i = 0; i < LEN(ctable); i++) {
		for (pp = &ctable[i]; (e = *pp);) {
			if (e->expire < now) {
				*pp = e->next;
				free(e->key);
				free(e->val);
				free(e);
				ccount--;
			} else
				pp = &e->next;
		}
	}
}

const char *
cache_get(const char *key)
{
	struct centry *e;

	for (e = ctable[strhash(key) % LEN(ctable)]; e; e = e->next)
		if (!strcmp(e->key, key))
			return e->expire >= now_ms() ? e->val : NULL;
	return NULL;
}

void
cache_set(const char *key, const char *val, int ttl)
{
	struct centry *e, **head = &ctable[strhash(key) % LEN(ctable)];

	for (e = *head; e; e = e->next) {
		if (!strcmp(e->key, key)) {
			free(e->val);
			e->val = xstrdup(val);
			e->expire = now_ms() + ttl * 1e3;
			return;
		}
	}
	if (ccount >= 4096)
		cache_purge();
	/* still full: drop this key's bucket rather than every entry */
	while (ccount >= 4096 && (e = *head)) {
		*head = e->next;
		free(e->key);
		free(e->val);
		free(e);
		ccount--;
	}
	e = xmalloc(sizeof(*e));
	e->key = xstrdup(key);
	e->val = xstrdup(val);
	e->expire = now_ms() + ttl * 1e3;
	e->next = *head;
	*head = e;
	ccount++;
}

int
mkdirs(char *p)
{
	char *s;

	for (s = p + 1; *s; s++) {
		if (*s != '/')
			continue;
		*s = 0;
		if (mkdir(p, 0700) < 0 && errno != EEXIST)
			return -1;
		*s = '/';
	}
	return mkdir(p, 0700) < 0 && errno != EEXIST ? -1 : 0;
}

const char *
cache_base(void)
{
	static char *base;
	const char *env, *parent = "/tmp", *home = "";
	size_t n;

	if (base)
		return base;
	if (cache_dir && *cache_dir)
		base = xstrdup(cache_dir);
	else {
		if ((env = getenv("XDG_CACHE_HOME")) && *env)
			parent = env;
		else if ((env = getenv("HOME")) && *env) {
			parent = env;
			home = "/.cache";
		}
		base = xmalloc((n = strlen(parent) + strlen(home) + 6));
		snprintf(base, n, "%s%s/priv", parent, home);
	}
	if (mkdirs(base) < 0)
		die("cache directory %s: %s", base, strerror(errno));
	return base;
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

static const uint32_t K[64] = {
    0x428a2f98,
    0x71374491,
    0xb5c0fbcf,
    0xe9b5dba5,
    0x3956c25b,
    0x59f111f1,
    0x923f82a4,
    0xab1c5ed5,
    0xd807aa98,
    0x12835b01,
    0x243185be,
    0x550c7dc3,
    0x72be5d74,
    0x80deb1fe,
    0x9bdc06a7,
    0xc19bf174,
    0xe49b69c1,
    0xefbe4786,
    0x0fc19dc6,
    0x240ca1cc,
    0x2de92c6f,
    0x4a7484aa,
    0x5cb0a9dc,
    0x76f988da,
    0x983e5152,
    0xa831c66d,
    0xb00327c8,
    0xbf597fc7,
    0xc6e00bf3,
    0xd5a79147,
    0x06ca6351,
    0x14292967,
    0x27b70a85,
    0x2e1b2138,
    0x4d2c6dfc,
    0x53380d13,
    0x650a7354,
    0x766a0abb,
    0x81c2c92e,
    0x92722c85,
    0xa2bfe8a1,
    0xa81a664b,
    0xc24b8b70,
    0xc76c51a3,
    0xd192e819,
    0xd6990624,
    0xf40e3585,
    0x106aa070,
    0x19a4c116,
    0x1e376c08,
    0x2748774c,
    0x34b0bcb5,
    0x391c0cb3,
    0x4ed8aa4a,
    0x5b9cca4f,
    0x682e6ff3,
    0x748f82ee,
    0x78a5636f,
    0x84c87814,
    0x8cc70208,
    0x90befffa,
    0xa4506ceb,
    0xbef9a3f7,
    0xc67178f2,
};

#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void
sha256_block(struct sha256 *s, const unsigned char *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		    (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = s->h[0];
	b = s->h[1];
	c = s->h[2];
	d = s->h[3];
	e = s->h[4];
	f = s->h[5];
	g = s->h[6];
	h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
	s->h[5] += f;
	s->h[6] += g;
	s->h[7] += h;
}

void
sha256_init(struct sha256 *s)
{
	static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
	    0x1f83d9ab, 0x5be0cd19};

	memcpy(s->h, iv, sizeof(iv));
	s->len = s->n = 0;
}

void
sha256_update(struct sha256 *s, const void *data, size_t len)
{
	const unsigned char *p = data;

	s->len += len;
	while (len--) {
		s->buf[s->n++] = *p++;
		if (s->n == 64) {
			sha256_block(s, s->buf);
			s->n = 0;
		}
	}
}

void
sha256_final(struct sha256 *s, unsigned char out[32])
{
	uint64_t bits = s->len * 8;
	unsigned char pad = 0x80, len[8];
	int i;

	sha256_update(s, &pad, 1);
	pad = 0;
	while (s->n != 56)
		sha256_update(s, &pad, 1);
	for (i = 0; i < 8; i++)
		len[i] = bits >> (56 - 8 * i);
	sha256_update(s, len, 8);
	for (i = 0; i < 32; i++)
		out[i] = s->h[i / 4] >> (24 - 8 * (i % 4));
}
