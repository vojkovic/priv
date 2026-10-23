#ifndef FETCH_H
#define FETCH_H

#include <curl/curl.h>

#include "util.h"

struct fetch;
typedef void (*fetch_cb)(struct fetch *f, void *arg);

struct fetch {
	CURL *h;
	struct curl_slist *headers;
	struct buf body;
	struct buf cookies;
	char *post;
	char server[64];
	long status;
	CURLcode code;
	char error[CURL_ERROR_SIZE];
	const char *impersonate;
	int default_headers;
	long http_version;
	size_t max_body;
	fetch_cb cb;
	void *arg;
	int running;
};

void fetch_init(void);
struct fetch *fetch_new(const char *url);
void fetch_header(struct fetch *f, const char *fmt, ...);
void fetch_cookie(struct fetch *f, const char *name, const char *val);
void fetch_post(struct fetch *f, const char *body, const char *content_type);
void fetch_impersonate(struct fetch *f, const char *target, int default_headers);
void fetch_timeout(struct fetch *f, long ms);
void fetch_start(struct fetch *f, fetch_cb cb, void *arg);
void fetch_free(struct fetch *f);
const char *fetch_url(struct fetch *f);
const char *fetch_host(struct fetch *f);
const char *fetch_body(struct fetch *f);
const char *fetch_content_type(struct fetch *f);

#endif
