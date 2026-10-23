#ifndef FAVICON_H
#define FAVICON_H

#include "http.h"
#include "util.h"

void favicon_init(void);
void favicon_prefetch(const char *authority);
char *favicon_inline(struct arena *a, const char *authority, size_t max);
void favicon_serve(struct conn *c, struct request *r);

#endif
