#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "favicon.h"
#include "fetch.h"
#include "proxy.h"

#define MAGIC "privy-favicon 1\n"
#define MAX_AGE "604800"
#define BLOB_MAX (20 << 10)
#define MAX_INFLIGHT 16

#define DEFAULT_FAVICON \
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#8087a2\" " \
	"stroke-width=\"1.5\"><circle cx=\"12\" cy=\"12\" r=\"10\"/><path d=\"M2 12h20M12 2a15 15 0 0 1 0 20" \
	"M12 2a15 15 0 0 0 0 20\"/></svg>"

struct waiter;

struct fav {
	struct fav *next;
	char *authority;
	char mime[64];
	size_t size;
	time_t stored;
	int cached, fetching;
	struct waiter *waiters;
};

struct waiter {
	struct conn *c;
	struct fav *f;
	struct waiter *next;
};

static struct fav *table[4096];
static char *dir;
static size_t total;
static int inflight;

static uint64_t
hash64(const char *s)
{
	uint64_t h = 14695981039346656037ull;

	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * 1099511628211ull;
	return h;
}

static char *
path(const char *authority)
{
	static char p[4096];

	snprintf(p, sizeof(p), "%s/%016llx", dir, (unsigned long long)hash64(authority));
	return p;
}

static struct fav *
find(const char *authority, int create)
{
	struct fav **head = &table[hash64(authority) % LEN(table)], *f;

	for (f = *head; f; f = f->next)
		if (!strcmp(f->authority, authority))
			return f;
	if (!create)
		return NULL;
	f = xcalloc(1, sizeof(*f));
	f->authority = xstrdup(authority);
	f->next = *head;
	*head = f;
	return f;
}

static int
fresh(struct fav *f)
{
	return f->cached && time(NULL) - f->stored < favicon_hold_days * 86400L;
}

static void
drop(struct fav *f)
{
	if (!f->cached)
		return;
	unlink(path(f->authority));
	total -= f->size;
	f->cached = 0;
}

static void
evict(void)
{
	struct fav *f, *oldest;
	size_t i;

	while (total > favicon_cache_bytes) {
		oldest = NULL;
		for (i = 0; i < LEN(table); i++)
			for (f = table[i]; f; f = f->next)
				if (f->cached && (!oldest || f->stored < oldest->stored))
					oldest = f;
		if (!oldest)
			return;
		drop(oldest);
	}
}

static void
load(const char *name)
{
	char p[4096], authority[512], mime[64];
	struct stat st;
	struct fav *f;
	FILE *fp;
	size_t n;

	snprintf(p, sizeof(p), "%s/%s", dir, name);
	if (!(fp = fopen(p, "rb")))
		return;
	if (!fgets(authority, sizeof(authority), fp) || strcmp(authority, MAGIC) ||
	    !fgets(authority, sizeof(authority), fp) || !fgets(mime, sizeof(mime), fp) || fstat(fileno(fp), &st) < 0) {
		fclose(fp);
		unlink(p);
		return;
	}
	fclose(fp);
	authority[strcspn(authority, "\n")] = 0;
	mime[strcspn(mime, "\n")] = 0;
	if (strcmp(path(authority), p)) {
		unlink(p);
		return;
	}
	f = find(authority, 1);
	snprintf(f->mime, sizeof(f->mime), "%s", mime);
	n = st.st_size;
	f->size = n;
	f->stored = st.st_mtime;
	f->cached = 1;
	total += n;
	if (!fresh(f))
		drop(f);
}

void
favicon_init(void)
{
	const char *base = cache_base();
	struct dirent *de;
	DIR *d;

	dir = xmalloc(strlen(base) + 10);
	snprintf(dir, strlen(base) + 10, "%s/favicons", base);
	if (mkdirs(dir) < 0)
		die("favicon cache %s: %s", dir, strerror(errno));
	if (!(d = opendir(dir)))
		die("favicon cache %s: %s", dir, strerror(errno));
	while ((de = readdir(d)))
		if (strlen(de->d_name) == 16)
			load(de->d_name);
	closedir(d);
	evict();
}

static void
respond_default(struct conn *c)
{
	http_respond(c, 200, "Content-Type: image/svg+xml\r\nCache-Control: max-age=" MAX_AGE "\r\n", DEFAULT_FAVICON,
	    sizeof(DEFAULT_FAVICON) - 1);
}

static void
respond_data(struct conn *c, const char *mime, const char *data, size_t len)
{
	struct buf h = {0};

	buf_printf(&h, "Content-Type: %s\r\nCache-Control: max-age=" MAX_AGE "\r\n", mime);
	http_respond(c, 200, buf_str(&h), data, len);
	buf_free(&h);
}

static int
skip_lines(FILE *fp, int n)
{
	char line[512];

	while (n-- > 0)
		if (!fgets(line, sizeof(line), fp))
			return -1;
	return 0;
}

static char *
read_blob(struct fav *f, size_t *len)
{
	char *data;
	FILE *fp;
	long off;

	if (!(fp = fopen(path(f->authority), "rb")))
		return NULL;
	if (skip_lines(fp, 3) < 0 || (off = ftell(fp)) < 0 || (size_t)off > f->size) {
		fclose(fp);
		return NULL;
	}
	data = xmalloc(f->size - off + 1);
	*len = fread(data, 1, f->size - off, fp);
	fclose(fp);
	if (*len != f->size - (size_t)off) {
		free(data);
		return NULL;
	}
	return data;
}

static int
respond_disk(struct conn *c, struct fav *f)
{
	char *data;
	size_t n;

	if (!*f->mime) {
		respond_default(c);
		return 0;
	}
	if (!(data = read_blob(f, &n)))
		return -1;
	respond_data(c, f->mime, data, n);
	free(data);
	return 0;
}

char *
favicon_inline(struct arena *a, const char *authority, size_t max)
{
	struct fav *f = authority ? find(authority, 0) : NULL;
	char *data, *r;
	size_t n;

	if (!f || !fresh(f))
		return NULL;
	if (!*f->mime)
		return aprintf(a, "data:image/svg+xml;base64,%s",
		    base64(a, (const unsigned char *)DEFAULT_FAVICON, sizeof(DEFAULT_FAVICON) - 1));
	if (f->size > max + 512 || !starts_with(f->mime, "image/") || !(data = read_blob(f, &n)))
		return NULL;
	r = n <= max ? aprintf(a, "data:%s;base64,%s", f->mime, base64(a, (unsigned char *)data, n)) : NULL;
	free(data);
	return r;
}

static void
store(struct fav *f, const char *mime, const char *data, size_t len)
{
	char tmp[4200];
	FILE *fp;
	size_t n;

	drop(f);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path(f->authority));
	if (!(fp = fopen(tmp, "wb")))
		return;
	n = fprintf(fp, MAGIC "%s\n%s\n", f->authority, mime);
	if (len)
		n += fwrite(data, 1, len, fp);
	if (fclose(fp) != 0 || rename(tmp, path(f->authority)) < 0) {
		unlink(tmp);
		return;
	}
	snprintf(f->mime, sizeof(f->mime), "%s", mime);
	f->size = n;
	f->stored = time(NULL);
	f->cached = 1;
	total += n;
	evict();
}

static void
unwait(void *arg)
{
	struct waiter *w = arg, **pp;

	for (pp = &w->f->waiters; *pp; pp = &(*pp)->next)
		if (*pp == w) {
			*pp = w->next;
			break;
		}
	free(w);
}

static void
fetched(struct fetch *ft, void *arg)
{
	struct fav *f = arg;
	struct waiter *w, *next;
	const char *ct = fetch_content_type(ft);
	char mime[64];
	int ok = ft->code == CURLE_OK && ft->status == 200 && ft->body.len && *ct;

	inflight--;
	f->fetching = 0;
	snprintf(mime, sizeof(mime), "%s", ok ? ct : "");
	if (ok && ft->body.len <= BLOB_MAX)
		store(f, mime, ft->body.p, ft->body.len);
	else if (!ok && ft->code == CURLE_OK)
		store(f, "", NULL, 0);
	w = f->waiters;
	f->waiters = NULL;
	for (; w; w = next) {
		next = w->next;
		http_on_close(w->c, NULL, NULL);
		if (ok)
			respond_data(w->c, mime, ft->body.p, ft->body.len);
		else
			respond_default(w->c);
		free(w);
	}
	fetch_free(ft);
}

static void
start(struct fav *f)
{
	struct buf url = {0};
	struct fetch *ft;

	f->fetching = 1;
	inflight++;
	buf_puts(&url, "https://news.kagi.com/api/favicon-proxy?domain=");
	url_quote(&url, f->authority, ".:", 0);
	buf_puts(&url, "&quality=fast");
	ft = fetch_new(buf_str(&url));
	buf_free(&url);
	curl_easy_setopt(ft->h, CURLOPT_FOLLOWLOCATION, 1L);
	ft->max_body = 1 << 20;
	fetch_timeout(ft, 3000);
	fetch_start(ft, fetched, f);
}

void
favicon_prefetch(const char *authority)
{
	struct fav *f;

	if (!authority || !*authority || strchr(authority, '/') || inflight >= MAX_INFLIGHT)
		return;
	f = find(authority, 1);
	if (!f->fetching && !fresh(f))
		start(f);
}

void
favicon_serve(struct conn *c, struct request *r)
{
	struct arena a = {0};
	const char *authority = query_param(&a, r->query, "authority");
	struct waiter *w;
	struct fav *f;

	if (!authority || !*authority || strchr(authority, '/') ||
	    !proxy_hmac_ok(authority, query_param(&a, r->query, "h"))) {
		arena_free(&a);
		http_respond(c, 400, "Content-Type: text/plain\r\n", "", 0);
		return;
	}
	f = find(authority, 1);
	arena_free(&a);
	if (fresh(f) && respond_disk(c, f) == 0)
		return;
	if (f->cached)
		drop(f);
	w = xmalloc(sizeof(*w));
	w->c = c;
	w->f = f;
	w->next = f->waiters;
	f->waiters = w;
	http_on_close(c, unwait, w);
	if (!f->fetching)
		start(f);
}
