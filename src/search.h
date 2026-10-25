#ifndef SEARCH_H
#define SEARCH_H

#include <stdint.h>

#include "ev.h"
#include "http.h"
#include "locales.h"
#include "prefs.h"
#include "result.h"

struct job;

struct category {
	const char *name, *label, *icon;
};

extern const struct category categories[];
extern const size_t ncategories;

struct filters {
	const char *category;
	const char *language;
	const struct locale *locale, *browser;
	const char *time_range;
	int safesearch;
	uint64_t engines;
	double timeout;
	struct prefs prefs;
};

struct search {
	struct conn *c;
	struct arena a;
	const char *raw, *query;
	int pageno;
	const char *tokens;
	struct filters filters;
	struct results res;
	struct job *jobs;
	size_t njobs;
	int pending, starting;
	struct ev_timer deadline;
	double start, elapsed;
};

void search_start(struct conn *c, const char *raw, int pageno, const char *tokens, const struct filters *f);
void filters_parse(struct arena *a, struct request *r, struct filters *f);
const struct locale *client_locale(struct arena *a, const char *header);
const char *query_parse(struct arena *a, const char *raw, struct filters *f);

#endif
