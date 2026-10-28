#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "core/ev.h"
#include "core/util.h"

//
// The event loop: epoll for sockets and a heap of timers, in one thread
//
// everything else in the program is a callback that runs from here, so none
// of it may block. timers sit in a min-heap ordered by when they fire.
//

struct watch {
	ev_fd_cb cb;
	void *arg;
	int events;
};

static int pfd = -1;
static struct watch *watches;
static int nwatches;
static struct ev_timer **heap;
static size_t nheap, capheap;

void
ev_init(void)
{
	pfd = epoll_create1(EPOLL_CLOEXEC);
	if (pfd < 0)
		die("event queue: %s", strerror(errno));
}

void
ev_watch(int fd, int events, ev_fd_cb cb, void *arg)
{
	struct epoll_event ev = {0};
	int old;

	if (fd >= nwatches) {
		int n = MAX(fd + 1, nwatches * 2);
		watches = xrealloc(watches, n * sizeof(*watches));
		memset(watches + nwatches, 0, (n - nwatches) * sizeof(*watches));
		nwatches = n;
	}
	old = watches[fd].events;
	watches[fd].cb = cb;
	watches[fd].arg = arg;
	watches[fd].events = events;
	if (old == events)
		return;
	ev.data.fd = fd;
	if (events & EV_READ)
		ev.events |= EPOLLIN | EPOLLRDHUP;
	if (events & EV_WRITE)
		ev.events |= EPOLLOUT;
	epoll_ctl(pfd, !old ? EPOLL_CTL_ADD : !events ? EPOLL_CTL_DEL : EPOLL_CTL_MOD, fd, &ev);
}

static void
heap_swap(size_t a, size_t b)
{
	struct ev_timer *t = heap[a];

	heap[a] = heap[b];
	heap[b] = t;
	heap[a]->idx = a;
	heap[b]->idx = b;
}

static void
heap_up(size_t i)
{
	while (i && heap[(i - 1) / 2]->when > heap[i]->when) {
		heap_swap(i, (i - 1) / 2);
		i = (i - 1) / 2;
	}
}

static void
heap_down(size_t i)
{
	size_t l, r, m;

	for (;;) {
		l = 2 * i + 1;
		r = l + 1;
		m = i;
		if (l < nheap && heap[l]->when < heap[m]->when)
			m = l;
		if (r < nheap && heap[r]->when < heap[m]->when)
			m = r;
		if (m == i)
			return;
		heap_swap(i, m);
		i = m;
	}
}

void
ev_timer_stop(struct ev_timer *t)
{
	size_t i = t->idx;

	if (!t->active)
		return;
	t->active = 0;
	nheap--;
	if (i != nheap) {
		heap[i] = heap[nheap];
		heap[i]->idx = i;
		heap_down(i);
		heap_up(i);
	}
}

void
ev_timer_start(struct ev_timer *t, double ms, ev_timer_cb cb, void *arg)
{
	ev_timer_stop(t);
	if (nheap == capheap) {
		capheap = capheap ? capheap * 2 : 64;
		heap = xrealloc(heap, capheap * sizeof(*heap));
	}
	t->when = now_ms() + ms;
	t->cb = cb;
	t->arg = arg;
	t->active = 1;
	t->idx = nheap;
	heap[nheap++] = t;
	heap_up(t->idx);
}

static void
run_timers(void)
{
	struct ev_timer *t;
	double now = now_ms();

	while (nheap && heap[0]->when <= now) {
		// stop it before the callback, which may restart it or free whatever holds it
		t = heap[0];
		ev_timer_stop(t);
		t->cb(t->arg);
	}
}

static void
dispatch(int fd, int ev)
{
	if (fd < nwatches && (watches[fd].events & ev) && watches[fd].cb)
		watches[fd].cb(fd, ev, watches[fd].arg);
}

void
ev_run(void)
{
	struct epoll_event evs[256];
	int n, i, timeout;

	for (;;) {
		timeout = -1;
		if (nheap) {
			double d = heap[0]->when - now_ms();
			timeout = d < 0 ? 0 : (int)d + 1;
		}
		n = epoll_wait(pfd, evs, LEN(evs), timeout);

		// sockets first, then whatever timers came due
		for (i = 0; i < n; i++) {
			int fd = evs[i].data.fd;
			if (evs[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP))
				dispatch(fd, EV_READ);
			if (evs[i].events & (EPOLLOUT | EPOLLHUP | EPOLLERR))
				dispatch(fd, EV_WRITE);
		}
		if (n < 0 && errno != EINTR)
			die("event wait: %s", strerror(errno));
		run_timers();
	}
}
