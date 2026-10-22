#ifndef PROXY_H
#define PROXY_H

#include "http.h"
#include "util.h"

void proxy_init(void);
char *image_url(struct arena *a, const char *url);
char *favicon_url(struct arena *a, const char *authority);
void proxy_image(struct conn *c, struct request *r);
int proxy_hmac_ok(const char *msg, const char *h);

#endif
