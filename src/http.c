#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ev.h"
#include "http.h"
#include "util.h"

#define IDLE_MS 30000

struct conn {
	int fd;
	char in[8192];
	size_t inlen;
	char hdr[8192];
	struct buf out;
	size_t outoff;
	int busy, keepalive, chunked, head, closing, dead, processing;
	char peer[64];
	struct ev_timer timer;
	void (*on_close)(void *);
	void *close_arg;
};

static int lfd = -1;
static http_handler handler;
static struct ev_timer accept_timer;

static void on_conn(int fd, int events, void *arg);
static void process(struct conn *c);

static void
conn_close(void *arg)
{
	struct conn *c = arg;

	if (c->on_close)
		c->on_close(c->close_arg);
	ev_timer_stop(&c->timer);
	ev_watch(c->fd, 0, NULL, NULL);
	close(c->fd);
	buf_free(&c->out);
	free(c);
}

static void
conn_kill(struct conn *c)
{
	if (c->dead)
		return;
	c->dead = 1;
	ev_watch(c->fd, 0, NULL, NULL);
	ev_timer_start(&c->timer, 0, conn_close, c);
}

static void
flush(struct conn *c)
{
	ssize_t n;

	while (c->outoff < c->out.len) {
		n = write(c->fd, c->out.p + c->outoff, c->out.len - c->outoff);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			ev_watch(c->fd, EV_READ | EV_WRITE, on_conn, c);
			return;
		}
		if (n <= 0) {
			conn_kill(c);
			return;
		}
		c->outoff += n;
	}
	c->out.len = c->outoff = 0;
	if (c->closing && !c->busy) {
		conn_kill(c);
		return;
	}
	ev_watch(c->fd, EV_READ, on_conn, c);
}

static const char *
status_text(int status)
{
	switch (status) {
	case 200: return "OK";
	case 302: return "Found";
	case 304: return "Not Modified";
	case 400: return "Bad Request";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	default: return "Internal Server Error";
	}
}

static void
put_head(struct conn *c, int status, const char *headers, long len)
{
	buf_printf(&c->out, "HTTP/1.1 %d %s\r\n", status, status_text(status));
	if (len >= 0 && status != 304)
		buf_printf(&c->out, "Content-Length: %ld\r\n", len);
	else if (len < 0)
		buf_puts(&c->out, "Transfer-Encoding: chunked\r\n");
	buf_printf(&c->out, "Connection: %s\r\n", c->keepalive ? "keep-alive" : "close");
	if (headers)
		buf_puts(&c->out, headers);
	buf_puts(&c->out, "\r\n");
}

static void
finish(struct conn *c)
{
	c->busy = 0;
	c->on_close = NULL;
	if (!c->keepalive)
		c->closing = 1;
	flush(c);
	if (c->dead)
		return;
	ev_timer_start(&c->timer, IDLE_MS, conn_close, c);
	if (!c->processing)
		process(c);
}

void
http_respond(struct conn *c, int status, const char *headers, const char *body, size_t len)
{
	if (c->dead)
		return;
	put_head(c, status, headers, len);
	if (!c->head && status != 304)
		buf_add(&c->out, body, len);
	finish(c);
}

void
http_begin(struct conn *c, int status, const char *headers)
{
	if (c->dead)
		return;
	c->chunked = 1;
	put_head(c, status, headers, -1);
	flush(c);
}

void
http_write(struct conn *c, const char *p, size_t n)
{
	if (c->dead || c->head || !n)
		return;
	buf_printf(&c->out, "%zx\r\n", n);
	buf_add(&c->out, p, n);
	buf_puts(&c->out, "\r\n");
	flush(c);
}

void
http_end(struct conn *c)
{
	http_end_trailers(c, "");
}

void
http_end_trailers(struct conn *c, const char *trailers)
{
	if (c->dead)
		return;
	if (!c->head) {
		buf_puts(&c->out, "0\r\n");
		buf_puts(&c->out, trailers);
		buf_puts(&c->out, "\r\n");
	}
	c->chunked = 0;
	c->on_close = NULL;
	finish(c);
}

void
http_on_close(struct conn *c, void (*cb)(void *), void *arg)
{
	c->on_close = cb;
	c->close_arg = arg;
}

static void
bad(struct conn *c, int status)
{
	c->keepalive = 0;
	c->busy = 1;
	http_respond(c, status, "Content-Type: text/plain\r\n", status_text(status), strlen(status_text(status)));
}

static char *
header(char *line, const char *name)
{
	size_t n = strlen(name);

	if (strncasecmp(line, name, n) || line[n] != ':')
		return NULL;
	line += n + 1;
	while (*line == ' ' || *line == '\t')
		line++;
	return str_strip(line);
}

static void
handle(struct conn *c, size_t hlen)
{
	struct request r = {0};
	char *line, *next, *sp, *target, *version, *v;
	int http11, close_hdr = 0, keep_hdr = 0;

	memcpy(c->hdr, c->in, hlen);
	c->hdr[hlen] = 0;
	memmove(c->in, c->in + hlen + 4, c->inlen - hlen - 4);
	c->inlen -= hlen + 4;

	line = c->hdr;
	next = strstr(line, "\r\n");
	if (next) {
		*next = 0;
		next += 2;
	}
	if (!(sp = strchr(line, ' ')))
		goto invalid;
	*sp = 0;
	target = sp + 1;
	if (!(sp = strchr(target, ' ')))
		goto invalid;
	*sp = 0;
	version = sp + 1;
	if (strncmp(version, "HTTP/1.", 7))
		goto invalid;
	http11 = strcmp(version, "HTTP/1.0") != 0;
	r.method = line;
	r.path = target;
	r.query = "";
	if ((sp = strchr(target, '?'))) {
		*sp = 0;
		r.query = sp + 1;
	}
	r.host = "";
	r.if_none_match = "";
	r.proto = "http";
	r.accept = r.user_agent = r.sec_ch_ua = r.accept_language = "";
	r.accept_encoding = r.x_forwarded_for = r.x_real_ip = r.sec_fetch_mode = "";
	r.remote_addr = c->peer;
	r.has_accept = r.has_user_agent = 0;
	for (line = next; line && *line; line = next) {
		next = strstr(line, "\r\n");
		if (next) {
			*next = 0;
			next += 2;
		}
		if ((v = header(line, "Host")))
			r.host = v;
		else if ((v = header(line, "If-None-Match")))
			r.if_none_match = v;
		else if ((v = header(line, "X-Forwarded-Proto")) && !strcmp(v, "https"))
			r.proto = "https";
		else if ((v = header(line, "Accept"))) {
			r.accept = v;
			r.has_accept = 1;
		} else if ((v = header(line, "User-Agent"))) {
			r.user_agent = v;
			r.has_user_agent = 1;
		} else if ((v = header(line, "Accept-Encoding")))
			r.accept_encoding = v;
		else if ((v = header(line, "X-Forwarded-For")))
			r.x_forwarded_for = v;
		else if ((v = header(line, "X-Real-IP")))
			r.x_real_ip = v;
		else if ((v = header(line, "Sec-Fetch-Mode")))
			r.sec_fetch_mode = v;
		else if ((v = header(line, "Sec-CH-UA")))
			r.sec_ch_ua = v;
		else if ((v = header(line, "Accept-Language")))
			r.accept_language = v;
		else if ((v = header(line, "Connection"))) {
			close_hdr = strcasestr(v, "close") != NULL;
			keep_hdr = strcasestr(v, "keep-alive") != NULL;
		} else if (((v = header(line, "Content-Length")) && strcmp(v, "0")) ||
		    header(line, "Transfer-Encoding"))
			close_hdr = 1;
	}
	c->keepalive = http11 ? !close_hdr : keep_hdr && !close_hdr;
	c->head = !strcmp(r.method, "HEAD");
	r.head = c->head;
	c->busy = 1;
	ev_timer_stop(&c->timer);
	if (strcmp(r.method, "GET") && !c->head) {
		c->keepalive = 0;
		http_respond(c, 405, "Allow: GET, HEAD\r\nContent-Type: text/plain\r\n", "Method Not Allowed", 18);
		return;
	}
	handler(c, &r);
	return;
invalid:
	bad(c, 400);
}

static void
process(struct conn *c)
{
	char *end;

	c->processing = 1;
	while (!c->busy && !c->dead && !c->closing) {
		c->in[c->inlen] = 0;
		end = c->inlen >= 4 ? strstr(c->in, "\r\n\r\n") : NULL;
		if (!end) {
			if (c->inlen >= sizeof(c->in) - 1)
				bad(c, 431);
			break;
		}
		handle(c, end - c->in);
	}
	c->processing = 0;
}

static void
on_conn(int fd, int events, void *arg)
{
	struct conn *c = arg;
	ssize_t n;

	(void)fd;
	if (events & EV_WRITE) {
		flush(c);
		return;
	}
	if (c->inlen >= sizeof(c->in) - 1) {
		if (c->busy)
			ev_watch(c->fd, c->outoff < c->out.len ? EV_WRITE : 0, on_conn, c);
		return;
	}
	n = read(c->fd, c->in + c->inlen, sizeof(c->in) - 1 - c->inlen);
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return;
	if (n <= 0) {
		conn_kill(c);
		return;
	}
	c->inlen += n;
	if (!c->busy)
		process(c);
}

static void on_accept(int fd, int events, void *arg);

static void
resume_accept(void *arg)
{
	(void)arg;
	ev_watch(lfd, EV_READ, on_accept, NULL);
}

static void
on_accept(int fd, int events, void *arg)
{
	struct sockaddr_storage ss;
	socklen_t sl;
	struct conn *c;
	int cfd, one = 1;

	(void)events;
	(void)arg;
	for (;;) {
		sl = sizeof(ss);
		cfd = accept(fd, (struct sockaddr *)&ss, &sl);
		if (cfd < 0) {
			if (errno == EMFILE || errno == ENFILE) {
				ev_watch(lfd, 0, NULL, NULL);
				ev_timer_start(&accept_timer, 100, resume_accept, NULL);
			}
			return;
		}
		fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL) | O_NONBLOCK);
		fcntl(cfd, F_SETFD, FD_CLOEXEC);
		setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		c = xcalloc(1, sizeof(*c));
		c->fd = cfd;
		if (ss.ss_family == AF_INET)
			inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, c->peer, sizeof(c->peer));
		else if (ss.ss_family == AF_INET6)
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&ss)->sin6_addr, c->peer, sizeof(c->peer));
		ev_watch(cfd, EV_READ, on_conn, c);
		ev_timer_start(&c->timer, IDLE_MS, conn_close, c);
	}
}

void
http_listen(const char *addr, int port, http_handler h)
{
	struct addrinfo hints = {0}, *res;
	char portstr[16];
	int one = 1, err;

	handler = h;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	snprintf(portstr, sizeof(portstr), "%d", port);
	if ((err = getaddrinfo(addr, portstr, &hints, &res)))
		die("%s: %s", addr, gai_strerror(err));
	lfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (lfd < 0)
		die("socket: %s", strerror(errno));
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(lfd, res->ai_addr, res->ai_addrlen) < 0)
		die("bind %s:%d: %s", addr, port, strerror(errno));
	if (listen(lfd, 1024) < 0)
		die("listen: %s", strerror(errno));
	freeaddrinfo(res);
	fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL) | O_NONBLOCK);
	fcntl(lfd, F_SETFD, FD_CLOEXEC);
	ev_watch(lfd, EV_READ, on_accept, NULL);
}
