#ifndef STATS_H
#define STATS_H

#include <stddef.h>

#define STATS_WINDOW 50

struct engine;

enum outcome { OUTCOME_OK, OUTCOME_EMPTY, OUTCOME_ERROR, OUTCOME_TIMEOUT };

/* how one engine has been answering */
struct engine_stats {
	struct {
		unsigned char outcome;
		float seconds;
	} recent[STATS_WINDOW];
	size_t nrecent, next;
	char error[160];
};

void stats_record(const struct engine *e, enum outcome o, double seconds, const char *error);
const struct engine_stats *stats_of(const struct engine *e);

#endif
