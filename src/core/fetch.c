#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "core/ev.h"
#include "core/fetch.h"

#define MAX_BODY (16 << 20)

static CURLM *multi;
static struct ev_timer timer;
static const char *cainfo, *capath;

static void
check_done(void)
{
	struct fetch *f;
	CURLMsg *msg;
	int left;

	while ((msg = curl_multi_info_read(multi, &left))) {
		if (msg->msg != CURLMSG_DONE)
			continue;
		curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&f);
		f->code = msg->data.result;
		curl_easy_getinfo(f->h, CURLINFO_RESPONSE_CODE, &f->status);
		curl_multi_remove_handle(multi, f->h);
		f->running = 0;
		f->cb(f, f->arg);
	}
}

static void
on_sock(int fd, int events, void *arg)
{
	int running;

	(void)arg;
	curl_multi_socket_action(multi, fd, events & EV_WRITE ? CURL_CSELECT_OUT : CURL_CSELECT_IN, &running);
	check_done();
}

static int
sock_cb(CURL *e, curl_socket_t s, int what, void *userp, void *socketp)
{
	static const int map[] = {[CURL_POLL_IN] = EV_READ,
	    [CURL_POLL_OUT] = EV_WRITE,
	    [CURL_POLL_INOUT] = EV_READ | EV_WRITE,
	    [CURL_POLL_REMOVE] = 0};

	(void)e;
	(void)userp;
	(void)socketp;
	ev_watch(s, what <= CURL_POLL_REMOVE ? map[what] : 0, on_sock, NULL);
	return 0;
}

static void
on_timer(void *arg)
{
	int running;

	(void)arg;
	curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &running);
	check_done();
}

static int
timer_cb(CURLM *m, long ms, void *userp)
{
	(void)m;
	(void)userp;
	if (ms < 0)
		ev_timer_stop(&timer);
	else
		ev_timer_start(&timer, ms, on_timer, NULL);
	return 0;
}

void
fetch_init(void)
{
	static const char *bundles[] = {
	    "/etc/ssl/cert.pem",
	    "/etc/ssl/certs/ca-certificates.crt",
	    "/etc/pki/tls/certs/ca-bundle.crt",
	    "/etc/ssl/ca-bundle.pem",
	};
	size_t i;

	if (curl_global_init(CURL_GLOBAL_ALL))
		die("curl_global_init failed");
	multi = curl_multi_init();
	curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
	curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, timer_cb);
	curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, 100L);
	curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

	cainfo = getenv("SSL_CERT_FILE");
	capath = getenv("SSL_CERT_DIR");
	for (i = 0; !cainfo && i < LEN(bundles); i++)
		if (!access(bundles[i], R_OK))
			cainfo = bundles[i];
}

static size_t
on_body(char *p, size_t size, size_t n, void *arg)
{
	struct fetch *f = arg;

	if (f->body.len + size * n > f->max_body)
		return 0;
	buf_add(&f->body, p, size * n);
	return size * n;
}

#define MAX_HEADERS (16 << 10)

static size_t
on_header(char *p, size_t size, size_t n, void *arg)
{
	struct fetch *f = arg;
	size_t len = size * n, vlen;

	if (f->response.len < MAX_HEADERS) {
		for (vlen = len; vlen && (p[vlen - 1] == '\r' || p[vlen - 1] == '\n'); vlen--)
			;
		buf_add(&f->response, p, vlen);
		buf_putc(&f->response, '\n');
	}
	if (len > 7 && !strncasecmp(p, "server:", 7)) {
		p += 7;
		len -= 7;
		while (len && (*p == ' ' || *p == '\t')) {
			p++;
			len--;
		}
		for (vlen = len; vlen && (p[vlen - 1] == '\r' || p[vlen - 1] == '\n' || p[vlen - 1] == ' '); vlen--)
			;
		vlen = MIN(vlen, sizeof(f->server) - 1);
		memcpy(f->server, p, vlen);
		f->server[vlen] = 0;
	}
	return size * n;
}

static int
private_v4(const unsigned char *a)
{
	return a[0] == 0 || a[0] == 10 || a[0] == 127 || (a[0] == 100 && (a[1] & 0xc0) == 64) ||
	    (a[0] == 169 && a[1] == 254) || (a[0] == 172 && (a[1] & 0xf0) == 16) || (a[0] == 192 && a[1] == 168) ||
	    (a[0] == 192 && a[1] == 0 && a[2] == 0) || (a[0] == 198 && (a[1] & 0xfe) == 18) || a[0] >= 224;
}

static int
private_addr(const struct curl_sockaddr *addr)
{
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
	const unsigned char *a;

	if (addr->family == AF_INET && addr->addrlen >= sizeof(v4)) {
		memcpy(&v4, &addr->addr, sizeof(v4));
		return private_v4((const unsigned char *)&v4.sin_addr);
	}
	if (addr->family != AF_INET6 || addr->addrlen < sizeof(v6))
		return 1;
	memcpy(&v6, &addr->addr, sizeof(v6));
	a = v6.sin6_addr.s6_addr;
	if (!memcmp(a, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12))
		return private_v4(a + 12);
	if (!memcmp(a, "\0\0\0\0\0\0\0\0\0\0\0\0", 12))
		return private_v4(a + 12) || !memcmp(a + 12, "\0\0\0\0", 4);
	/* nat64 and 6to4 carry an ipv4 address that may be a private one */
	if (!memcmp(a, "\0\x64\xff\x9b\0\0\0\0\0\0\0\0", 12))
		return private_v4(a + 12);
	if (a[0] == 0x20 && a[1] == 0x02)
		return private_v4(a + 2);
	return !memcmp(a, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 15) || (a[0] & 0xfe) == 0xfc ||
	    (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) || a[0] == 0xff;
}

/* upstream urls come from search results, so never let one (or its redirects) reach the local network */
static curl_socket_t
open_socket(void *arg, curlsocktype purpose, struct curl_sockaddr *addr)
{
	if (private_addr(addr))
		return CURL_SOCKET_BAD;
	return socket(addr->family, addr->socktype, addr->protocol);
}

struct fetch *
fetch_new(const char *url)
{
	struct fetch *f = xcalloc(1, sizeof(*f));

	f->h = curl_easy_init();
	if (!f->h)
		die("curl_easy_init failed");
	curl_easy_setopt(f->h, CURLOPT_URL, url);
	curl_easy_setopt(f->h, CURLOPT_PRIVATE, f);
	curl_easy_setopt(f->h, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(f->h, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(f->h, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(f->h, CURLOPT_HEADERDATA, f);
	curl_easy_setopt(f->h, CURLOPT_ERRORBUFFER, f->error);
	curl_easy_setopt(f->h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(f->h, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(f->h, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(f->h, CURLOPT_MAXREDIRS, 30L);
	curl_easy_setopt(f->h, CURLOPT_OPENSOCKETFUNCTION, open_socket);
	curl_easy_setopt(f->h, CURLOPT_MAXAGE_CONN, 86400L);
	if (cainfo)
		curl_easy_setopt(f->h, CURLOPT_CAINFO, cainfo);
	if (capath)
		curl_easy_setopt(f->h, CURLOPT_CAPATH, capath);
	curl_easy_setopt(f->h, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br, zstd");
	f->impersonate = impersonate_chrome;
	f->default_headers = 1;
	f->http_version = CURL_HTTP_VERSION_2_0;
	f->max_body = MAX_BODY;
	return f;
}

void
fetch_impersonate(struct fetch *f, const char *target, int default_headers)
{
	f->impersonate = target;
	f->default_headers = default_headers;
}

void
fetch_header(struct fetch *f, const char *fmt, ...)
{
	struct buf b = {0};
	va_list ap;

	va_start(ap, fmt);
	buf_vprintf(&b, fmt, ap);
	va_end(ap);
	f->headers = curl_slist_append(f->headers, buf_str(&b));
	buf_free(&b);
}

void
fetch_cookie(struct fetch *f, const char *name, const char *val)
{
	if (f->cookies.len)
		buf_puts(&f->cookies, "; ");
	buf_printf(&f->cookies, "%s=%s", name, val);
}

void
fetch_post(struct fetch *f, const char *body, const char *content_type)
{
	free(f->post);
	f->post = xstrdup(body);
	curl_easy_setopt(f->h, CURLOPT_POSTFIELDS, f->post);
	fetch_header(f, "Content-Type: %s", content_type);
}

void
fetch_timeout(struct fetch *f, long ms)
{
	curl_easy_setopt(f->h, CURLOPT_TIMEOUT_MS, MAX(ms, 1L));
}

void
fetch_start(struct fetch *f, fetch_cb cb, void *arg)
{
	f->cb = cb;
	f->arg = arg;
	if (f->impersonate)
		curl_easy_impersonate(f->h, f->impersonate, f->default_headers);
	curl_easy_setopt(f->h, CURLOPT_HTTP_VERSION, f->http_version);
	if (f->headers)
		curl_easy_setopt(f->h, CURLOPT_HTTPHEADER, f->headers);
	if (f->cookies.len)
		curl_easy_setopt(f->h, CURLOPT_COOKIE, f->cookies.p);
	f->running = 1;
	curl_multi_add_handle(multi, f->h);
}

void
fetch_free(struct fetch *f)
{
	if (!f)
		return;
	if (f->running)
		curl_multi_remove_handle(multi, f->h);
	curl_easy_cleanup(f->h);
	curl_slist_free_all(f->headers);
	buf_free(&f->body);
	buf_free(&f->cookies);
	buf_free(&f->response);
	free(f->post);
	free(f);
}

const char *
fetch_url(struct fetch *f)
{
	char *u = NULL;

	curl_easy_getinfo(f->h, CURLINFO_EFFECTIVE_URL, &u);
	return u ? u : "";
}

/* only the host is ever logged, since the rest of an upstream url can carry the user's query */
const char *
fetch_host(struct fetch *f)
{
	static char host[256];
	const char *u = fetch_url(f), *p = strstr(u, "://");
	size_t n;

	p = p ? p + 3 : u;
	n = MIN(strcspn(p, "/?#"), sizeof(host) - 1);
	memcpy(host, p, n);
	host[n] = 0;
	return host;
}

const char *
fetch_body(struct fetch *f)
{
	return buf_str(&f->body);
}

/* the first response header line starting with prefix, as "name: value" */
char *
fetch_find_header(struct fetch *f, const char *prefix)
{
	size_t n = strlen(prefix), len;
	const char *p = buf_str(&f->response), *end;
	char *line;

	for (; *p; p = end + (*end == '\n')) {
		end = strchrnul(p, '\n');
		if ((size_t)(end - p) > n && !strncasecmp(p, prefix, n)) {
			len = (size_t)(end - p);
			line = xmalloc(len + 1);
			memcpy(line, p, len);
			line[len] = 0;
			return line;
		}
	}
	return NULL;
}

const char *
fetch_content_type(struct fetch *f)
{
	char *ct = NULL;

	curl_easy_getinfo(f->h, CURLINFO_CONTENT_TYPE, &ct);
	return ct ? ct : "";
}
