#ifndef TOKEN_H
#define TOKEN_H

#include "engine.h"
#include "ev.h"

struct token_waiter;

struct token {
	const char *name;
	int ttl, refresh;
	void (*fetch)(struct token *t);
	char *value;
	double fetched_at, last_used;
	int fetching;
	struct ev_timer timer;
	struct token_waiter *waiters;
};

void token_warm(struct token *t);
void token_get(struct job *j, struct token *t, void (*cb)(struct job *j, const char *value));
void token_set(struct token *t, const char *value);
void token_fail(struct token *t, const char *error, int suspend);

#endif
