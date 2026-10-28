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

/* the only links taken from upstream data, so none can run script when clicked */
int
web_url(const char *s)
{
	return s && (!strncasecmp(s, "https://", 8) || !strncasecmp(s, "http://", 7));
}
