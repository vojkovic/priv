#ifndef METRICS_H
#define METRICS_H

#include "core/fetch.h"
#include "core/util.h"

struct engine;

/* codes if we got no HTTP response */
enum {
	METRICS_DEADLINE = 590, /* the search deadline passed and the request was cancelled */
	METRICS_TIMEOUT = 591, /* the request's own timeout ran out */
	METRICS_CONNECT = 592, /* could not resolve the host or connect to it */
	METRICS_TLS = 593, /* the TLS handshake or certificate check failed */
	METRICS_NETWORK = 594 /* any other transport failure */
};

void metrics_search(int status);
void metrics_fetch(const struct engine *e, const struct fetch *f);
void metrics_cancelled(const struct engine *e);
void metrics_render(struct buf *b);

#endif
