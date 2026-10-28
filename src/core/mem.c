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
// Memory and strings: allocation that exits on failure, a growing buffer
// (buf), and a bump allocator for a request's lifetime (arena)
//
// buf_html is the way text reaches a page, and it escapes by default: %s is
// escaped, %r is raw and only for constant markup, %u is url-encoded.
//

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
