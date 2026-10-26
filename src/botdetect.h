#ifndef BOTDETECT_H
#define BOTDETECT_H

#include "http.h"

void botdetect_init(void);
const char *botdetect_client_ip(struct request *r);
int botdetect(struct conn *c, struct request *r);
const char *botdetect_token(void);
void botdetect_ping(struct conn *c, struct request *r, const char *token);

#endif
