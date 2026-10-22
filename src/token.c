#include <stdlib.h>

#include "token.h"

#define IDLE_SECS 1800

struct token_waiter {
	struct token *t;
	struct job *j;
	void (*cb)(struct job *j, const char *value);
	struct token_waiter *next;
};

static void
start_fetch(struct token *t)
{
	if (t->fetching)
		return;
	t->fetching = 1;
	t->fetch(t);
}

static void
on_refresh(void *arg)
{
	struct token *t = arg;

	if (now_ms() - t->last_used < IDLE_SECS * 1e3)
		start_fetch(t);
}

static int
valid(struct token *t)
{
	return t->value && now_ms() - t->fetched_at < t->ttl * 1e3;
}

static void
unwait(void *arg)
{
	struct token_waiter *w = arg, **pp;

	if (w->t)
		for (pp = &w->t->waiters; *pp; pp = &(*pp)->next)
			if (*pp == w) {
				*pp = w->next;
				break;
			}
	free(w);
}

static struct token_waiter *
take_waiters(struct token *t)
{
	struct token_waiter *list = t->waiters, *w;

	t->waiters = NULL;
	for (w = list; w; w = w->next)
		w->t = NULL;
	return list;
}

void
token_warm(struct token *t)
{
	t->last_used = now_ms();
	start_fetch(t);
}

void
token_get(struct job *j, struct token *t, void (*cb)(struct job *j, const char *value))
{
	struct token_waiter *w;

	t->last_used = now_ms();
	if (valid(t)) {
		cb(j, astrdup(j->a, t->value));
		return;
	}
	w = xmalloc(sizeof(*w));
	w->t = t;
	w->j = j;
	w->cb = cb;
	w->next = t->waiters;
	t->waiters = w;
	job_on_cleanup(j, unwait, w);
	start_fetch(t);
}

void
token_set(struct token *t, const char *value)
{
	struct token_waiter *w, *next;

	free(t->value);
	t->value = xstrdup(value);
	t->fetched_at = now_ms();
	t->fetching = 0;
	ev_timer_start(&t->timer, t->refresh * 1e3, on_refresh, t);
	for (w = take_waiters(t); w; w = next) {
		next = w->next;
		w->cb(w->j, astrdup(w->j->a, value));
	}
}

void
token_fail(struct token *t, const char *error, int suspend)
{
	struct token_waiter *w, *next;

	fprintf(stderr, "%s: token: %s\n", t->name, error);
	t->fetching = 0;
	for (w = take_waiters(t); w; w = next) {
		next = w->next;
		job_fail(w->j, error, suspend);
	}
}
