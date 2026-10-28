#ifndef ROUTES_H
#define ROUTES_H

#include "core/http.h"

void routes_init(void);
void route(struct conn *c, struct request *r);

#endif
