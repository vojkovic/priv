#ifndef EV_H
#define EV_H

#include <stddef.h>

enum { EV_READ = 1, EV_WRITE = 2 };

typedef void (*ev_fd_cb)(int fd, int events, void *arg);
typedef void (*ev_timer_cb)(void *arg);

struct ev_timer {
	double when;
	ev_timer_cb cb;
	void *arg;
	size_t idx;
	int active;
};

void ev_init(void);
void ev_watch(int fd, int events, ev_fd_cb cb, void *arg);
void ev_timer_start(struct ev_timer *t, double ms, ev_timer_cb cb, void *arg);
void ev_timer_stop(struct ev_timer *t);
void ev_run(void);

#endif
