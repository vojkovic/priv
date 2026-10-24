#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "stats.h"

static struct engine_stats table[64];

const struct engine_stats *
stats_of(const struct engine *e)
{
	return e->index >= 0 && (size_t)e->index < LEN(table) ? &table[e->index] : NULL;
}

void
stats_record(const struct engine *e, enum outcome o, double seconds, const char *error)
{
	struct engine_stats *s;

	if (e->index < 0 || (size_t)e->index >= LEN(table))
		return;
	s = &table[e->index];
	s->recent[s->next].outcome = (unsigned char)o;
	s->recent[s->next].seconds = (float)seconds;
	s->next = (s->next + 1) % STATS_WINDOW;
	if (s->nrecent < STATS_WINDOW)
		s->nrecent++;
	if (error)
		snprintf(s->error, sizeof(s->error), "%s", error);
}
