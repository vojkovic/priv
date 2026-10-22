#ifndef SEARCH_H
#define SEARCH_H

#include <stdint.h>

#include "ev.h"
#include "http.h"
#include "locales.h"
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
	const struct locale *locale;
	const char *time_range;
	int safesearch;
	uint64_t engines;
	double timeout;
};

struct search {
	struct conn *c;
	struct arena a;
	const char *raw, *query;
	int pageno;
	struct filters filters;
	struct results res;
	struct job *jobs;
	size_t njobs;
	int pending, starting;
	struct ev_timer deadline;
	double start, elapsed;
};

void search_start(struct conn *c, const char *raw, int pageno, const struct filters *f);
void filters_parse(struct arena *a, struct request *r, struct filters *f);
const char *query_parse(struct arena *a, const char *raw, struct filters *f);

#endif
