#ifndef FAVICON_H
#define FAVICON_H

#include "http.h"

void favicon_init(void);
void favicon_prefetch(const char *authority);
void favicon_serve(struct conn *c, struct request *r);

#endif
