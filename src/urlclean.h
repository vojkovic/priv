#ifndef URLCLEAN_H
#define URLCLEAN_H

#include "util.h"

char *urlclean_unwrap(struct arena *a, const char *url);
void urlclean_strip(struct arena *a, struct url *u);

#endif
