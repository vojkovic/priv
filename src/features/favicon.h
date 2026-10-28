#ifndef FAVICON_H
#define FAVICON_H

#include "core/http.h"
#include "core/util.h"

#define FAVICON_DEFAULT "kagi"

/* a service that serves a site's icon at url, with the site in its %s */
struct favicon_resolver {
	const char *name, *label, *url, *miss_mime;
	size_t min_size;
};

extern const struct favicon_resolver favicon_resolvers[];
extern const size_t nfavicon_resolvers;

void favicon_init(void);
void favicon_prefetch(const char *resolver, const char *authority);
char *favicon_inline(struct arena *a, const char *resolver, const char *authority, size_t max);
void favicon_serve(struct conn *c, struct request *r);

#endif
