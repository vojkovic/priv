#include <stdio.h>

#include "engines/engine.h"
#include "features/metrics.h"

#define SLOTS 16

struct counts {
	long code[SLOTS];
	unsigned long n[SLOTS];
	int used;
};

static struct counts search_counts, engine_counts[MAX_ENGINES];

static void
bump(struct counts *c, long code)
{
	int i;

	for (i = 0; i < c->used && c->code[i] != code; i++)
		;
	if (i == c->used) {
		if (c->used < SLOTS - 1) {
			c->code[c->used++] = code;
		} else {
			i = SLOTS - 1;
			c->code[i] = -1;
			c->used = SLOTS;
		}
	}
	c->n[i]++;
}

void
metrics_search(int status)
{
	bump(&search_counts, status);
}

static void
engine_bump(const struct engine *e, long code)
{
	if (e->index >= 0 && (size_t)e->index < LEN(engine_counts))
		bump(&engine_counts[e->index], code);
}

void
metrics_fetch(const struct engine *e, const struct fetch *f)
{
	long code;

	switch (f->code) {
	case CURLE_OK: code = f->status; break;
	case CURLE_OPERATION_TIMEDOUT: code = METRICS_TIMEOUT; break;
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT: code = METRICS_CONNECT; break;
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_PEER_FAILED_VERIFICATION: code = METRICS_TLS; break;
	default: code = METRICS_NETWORK; break;
	}
	engine_bump(e, code);
}

void
metrics_cancelled(const struct engine *e)
{
	engine_bump(e, METRICS_DEADLINE);
}

static void
put_code(struct buf *b, long code)
{
	if (code >= 0)
		buf_printf(b, "code=\"%ld\"", code);
	else
		buf_puts(b, "code=\"other\"");
}

void
metrics_render(struct buf *b)
{
	const struct counts *c;
	size_t e;
	int i;

	buf_puts(b,
	    "# HELP priv_search_requests_total Responses to /search\n"
	    "# TYPE priv_search_requests_total counter\n");
	for (i = 0; i < search_counts.used; i++) {
		buf_puts(b, "priv_search_requests_total{");
		put_code(b, search_counts.code[i]);
		buf_printf(b, "} %lu\n", search_counts.n[i]);
	}
	buf_puts(b,
	    "# HELP priv_engine_requests_total Upstream requests\n"
	    "# TYPE priv_engine_requests_total counter\n");
	for (e = 0; e < nengines; e++) {
		c = &engine_counts[engines[e]->index];
		for (i = 0; i < c->used; i++) {
			buf_printf(b, "priv_engine_requests_total{engine=\"%s\",", engines[e]->name);
			put_code(b, c->code[i]);
			buf_printf(b, "} %lu\n", c->n[i]);
		}
	}
}
