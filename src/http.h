#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

struct conn;

struct request {
	const char *method;
	const char *path;
	const char *query;
	const char *host;
	const char *if_none_match;
	const char *proto;
	const char *accept;
	const char *user_agent;
	const char *sec_ch_ua;
	const char *accept_language;
	const char *accept_encoding;
	const char *x_forwarded_for;
	const char *x_real_ip;
	const char *sec_fetch_mode;
	const char *remote_addr;
	int head;
	int has_accept, has_user_agent;
};

typedef void (*http_handler)(struct conn *c, struct request *r);

void http_listen(const char *addr, int port, http_handler h);
void http_respond(struct conn *c, int status, const char *headers, const char *body, size_t len);
void http_begin(struct conn *c, int status, const char *headers);
void http_write(struct conn *c, const char *p, size_t n);
void http_end(struct conn *c);
void http_on_close(struct conn *c, void (*cb)(void *), void *arg);

#endif
