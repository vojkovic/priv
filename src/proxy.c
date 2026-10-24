#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fetch.h"
#include "proxy.h"

#define MAX_IMAGE (5 << 20)
/* sent for images that failed; privy.js recognises its odd 403x302 size and hides the result */
#define FALLBACK_IMAGE \
	"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"403\" height=\"302\" viewBox=\"0 0 403 302\">" \
	"<rect width=\"403\" height=\"302\" fill=\"#363a4f\"/><g transform=\"translate(159.5 109) scale(3.5)\" " \
	"fill=\"none\" stroke=\"#5b6078\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" \
	"<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><circle cx=\"8.5\" cy=\"8.5\" r=\"1.5\"/>" \
	"<path d=\"M21 15l-5-5L5 21\"/></g></svg>"
static unsigned char secret[32];

static void
hmac(const char *msg, char hex[65])
{
	unsigned char ipad[64], opad[64], inner[32], out[32];
	struct sha256 s;
	int i;

	memset(ipad, 0x36, sizeof(ipad));
	memset(opad, 0x5c, sizeof(opad));
	for (i = 0; i < (int)sizeof(secret); i++) {
		ipad[i] ^= secret[i];
		opad[i] ^= secret[i];
	}
	sha256_init(&s);
	sha256_update(&s, ipad, 64);
	sha256_update(&s, msg, strlen(msg));
	sha256_final(&s, inner);
	sha256_init(&s);
	sha256_update(&s, opad, 64);
	sha256_update(&s, inner, 32);
	sha256_final(&s, out);
	for (i = 0; i < 32; i++)
		snprintf(hex + 2 * i, 3, "%02x", out[i]);
}

int
proxy_hmac_ok(const char *msg, const char *h)
{
	char want[65];
	unsigned char diff = 0;
	int i;

	if (!h || strlen(h) != 64)
		return 0;
	hmac(msg, want);
	for (i = 0; i < 64; i++)
		diff |= want[i] ^ h[i];
	return !diff;
}

void
proxy_init(void)
{
	char p[4096];
	int fd;

	snprintf(p, sizeof(p), "%s/secret", cache_base());
	if ((fd = open(p, O_RDONLY)) >= 0) {
		if (read(fd, secret, sizeof(secret)) == (ssize_t)sizeof(secret)) {
			close(fd);
			return;
		}
		close(fd);
	}
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, secret, sizeof(secret)) != (ssize_t)sizeof(secret))
		die("cannot read /dev/urandom");
	close(fd);
	if ((fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0 ||
	    write(fd, secret, sizeof(secret)) != (ssize_t)sizeof(secret))
		fprintf(stderr, "cannot save %s, proxied urls will change on restart\n", p);
	if (fd >= 0)
		close(fd);
}

char *
image_url(struct arena *a, const char *url)
{
	static const char *types[] = {"gif;", "png;", "jpeg;", "pjpeg;", "webp;", "tiff;", "bmp;"};
	struct buf b = {0};
	char h[65], *r;
	size_t i;

	if (!url || !*url)
		return NULL;
	if (starts_with(url, "//"))
		url = aprintf(a, "https:%s", url);
	if (starts_with(url, "data:image/")) {
		for (i = 0; i < LEN(types); i++)
			if (starts_with(url + 11, types[i]) && starts_with(url + 11 + strlen(types[i]), "base64,"))
				return (char *)url;
		return NULL;
	}
	hmac(url, h);
	form_add(&b, "url", url);
	form_add(&b, "h", h);
	r = aprintf(a, "/image?%s", buf_str(&b));
	buf_free(&b);
	return r;
}

char *
favicon_url(struct arena *a, const char *authority)
{
	struct buf b = {0};
	char h[65], *r;

	if (!authority || !*authority)
		return NULL;
	hmac(authority, h);
	form_add(&b, "authority", authority);
	form_add(&b, "h", h);
	r = aprintf(a, "/favicon?%s", buf_str(&b));
	buf_free(&b);
	return r;
}

#define CACHE_TTL 120
#define FAILED_TTL 60
#define CACHE_BYTES (64 << 20)
#define MAX_PREFETCH 32

enum { FETCHING, READY, FAILED };

struct waiter {
	struct conn *c;
	struct image *img;
	struct waiter *next;
};

/* upstream images kept briefly so prefetches can be inlined or served without a second fetch */
struct image {
	struct image *next;
	char *url, *mime, *data;
	size_t len;
	int state;
	double expires;
	struct waiter *waiters;
};

static struct image *images[1024];
static size_t cached, prefetching;

static uint32_t
url_hash(const char *s)
{
	uint32_t h = 2166136261u;

	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

static const char *
normalize(struct arena *a, const char *url)
{
	return starts_with(url, "//") ? aprintf(a, "https:%s", url) : url;
}

static void
image_free(struct image *img)
{
	cached -= img->len;
	free(img->url);
	free(img->mime);
	free(img->data);
	free(img);
}

/* drops expired entries, then the soonest to expire until under the cap */
static void
sweep(void)
{
	struct image **pp, *img, **oldest;
	double now = now_ms();
	size_t i;

	for (i = 0; i < LEN(images); i++)
		for (pp = &images[i]; (img = *pp);)
			if (img->state != FETCHING && img->expires < now) {
				*pp = img->next;
				image_free(img);
			} else {
				pp = &img->next;
			}
	while (cached > CACHE_BYTES) {
		oldest = NULL;
		for (i = 0; i < LEN(images); i++)
			for (pp = &images[i]; *pp; pp = &(*pp)->next)
				if ((*pp)->state == READY && (!oldest || (*pp)->expires < (*oldest)->expires))
					oldest = pp;
		if (!oldest)
			return;
		img = *oldest;
		*oldest = img->next;
		image_free(img);
	}
}

static struct image *
lookup(const char *url)
{
	struct image *img;

	for (img = images[url_hash(url) % LEN(images)]; img; img = img->next)
		if (!strcmp(img->url, url) && (img->state == FETCHING || img->expires >= now_ms()))
			return img;
	return NULL;
}

static void
respond(struct conn *c, struct image *img)
{
	struct buf h = {0};

	if (img->state != READY) {
		http_respond(c, 200, "Content-Type: image/svg+xml\r\nCache-Control: max-age=3600\r\n" MEDIA_HEADERS,
		    FALLBACK_IMAGE, sizeof(FALLBACK_IMAGE) - 1);
		return;
	}
	buf_printf(&h, "Content-Type: %s\r\nCache-Control: max-age=86400\r\n" MEDIA_HEADERS, img->mime);
	http_respond(c, 200, buf_str(&h), img->data, img->len);
	buf_free(&h);
}

static void
fetched(struct fetch *f, void *arg)
{
	struct image *img = arg;
	const char *ct = fetch_content_type(f);
	struct waiter *w, *next;

	prefetching--;
	if (f->code != CURLE_OK || f->status != 200 ||
	    (!starts_with(ct, "image/") && !starts_with(ct, "binary/octet-stream"))) {
		fprintf(stderr, "image: %ld %s from %s\n", f->status, f->code ? curl_easy_strerror(f->code) : ct,
		    fetch_host(f));
		img->state = FAILED;
		img->expires = now_ms() + FAILED_TTL * 1e3;
	} else {
		img->state = READY;
		img->mime = xstrdup(ct);
		img->data = f->body.p;
		img->len = f->body.len;
		f->body.p = NULL;
		f->body.len = f->body.cap = 0;
		img->expires = now_ms() + CACHE_TTL * 1e3;
		cached += img->len;
	}
	fetch_free(f);
	for (w = img->waiters; w; w = next) {
		next = w->next;
		http_on_close(w->c, NULL, NULL);
		respond(w->c, img);
		free(w);
	}
	img->waiters = NULL;
	sweep();
}

static struct image *
fetch_image(const char *url)
{
	struct image *img = xcalloc(1, sizeof(*img));
	struct image **head = &images[url_hash(url) % LEN(images)];
	struct fetch *f = fetch_new(url);
	char *ua = gen_useragent();

	img->url = xstrdup(url);
	img->state = FETCHING;
	img->next = *head;
	*head = img;
	fetch_header(f, "User-Agent: %s", ua);
	free(ua);
	fetch_header(f, "Accept: image/webp,*/*");
	fetch_header(f, "Sec-GPC: 1");
	fetch_header(f, "DNT: 1");
	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	f->max_body = MAX_IMAGE;
	fetch_timeout(f, 10000);
	prefetching++;
	fetch_start(f, fetched, img);
	return img;
}

void
image_prefetch(const char *url)
{
	struct arena a = {0};

	if (url && *url && !starts_with(url, "data:") && prefetching < MAX_PREFETCH) {
		url = normalize(&a, url);
		if (!lookup(url))
			fetch_image(url);
	}
	arena_free(&a);
}

char *
image_inline(struct arena *a, const char *url, size_t max)
{
	struct image *img;

	if (!url || !*url || starts_with(url, "data:"))
		return NULL;
	img = lookup(normalize(a, url));
	if (!img || img->state != READY || img->len > max || !starts_with(img->mime, "image/"))
		return NULL;
	return aprintf(a, "data:%s;base64,%s", img->mime, base64(a, (unsigned char *)img->data, img->len));
}

static void
unwait(void *arg)
{
	struct waiter *w = arg, **pp;

	for (pp = &w->img->waiters; *pp; pp = &(*pp)->next)
		if (*pp == w) {
			*pp = w->next;
			break;
		}
	free(w);
}

static void
empty(struct conn *c, int status)
{
	http_respond(c, status, "Content-Type: text/plain\r\n", "", 0);
}

void
proxy_image(struct conn *c, struct request *r)
{
	struct arena a = {0};
	const char *url = query_param(&a, r->query, "url");
	struct image *img;
	struct waiter *w;

	if (!url || !*url || !proxy_hmac_ok(url, query_param(&a, r->query, "h"))) {
		arena_free(&a);
		empty(c, 400);
		return;
	}
	if (!(img = lookup(url)))
		img = fetch_image(url);
	arena_free(&a);
	if (img->state != FETCHING) {
		respond(c, img);
		return;
	}
	w = xmalloc(sizeof(*w));
	w->c = c;
	w->img = img;
	w->next = img->waiters;
	img->waiters = w;
	http_on_close(c, unwait, w);
}
