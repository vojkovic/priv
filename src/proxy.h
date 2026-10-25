#ifndef PROXY_H
#define PROXY_H

#include "http.h"
#include "util.h"

void proxy_init(void);
char *image_url(struct arena *a, const char *url);
char *favicon_url(struct arena *a, const char *authority, const char *resolver);
void proxy_image(struct conn *c, struct request *r);
void image_prefetch(const char *url);
char *image_inline(struct arena *a, const char *url, size_t max);
int proxy_hmac_ok(const char *msg, const char *h);

#endif
