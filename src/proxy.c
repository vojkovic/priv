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
struct sha256 {
	uint32_t h[8];
	uint64_t len;
	unsigned char buf[64];
	size_t n;
};

static const uint32_t K[64] = {
    0x428a2f98,
    0x71374491,
    0xb5c0fbcf,
    0xe9b5dba5,
    0x3956c25b,
    0x59f111f1,
    0x923f82a4,
    0xab1c5ed5,
    0xd807aa98,
    0x12835b01,
    0x243185be,
    0x550c7dc3,
    0x72be5d74,
    0x80deb1fe,
    0x9bdc06a7,
    0xc19bf174,
    0xe49b69c1,
    0xefbe4786,
    0x0fc19dc6,
    0x240ca1cc,
    0x2de92c6f,
    0x4a7484aa,
    0x5cb0a9dc,
    0x76f988da,
    0x983e5152,
    0xa831c66d,
    0xb00327c8,
    0xbf597fc7,
    0xc6e00bf3,
    0xd5a79147,
    0x06ca6351,
    0x14292967,
    0x27b70a85,
    0x2e1b2138,
    0x4d2c6dfc,
    0x53380d13,
    0x650a7354,
    0x766a0abb,
    0x81c2c92e,
    0x92722c85,
    0xa2bfe8a1,
    0xa81a664b,
    0xc24b8b70,
    0xc76c51a3,
    0xd192e819,
    0xd6990624,
    0xf40e3585,
    0x106aa070,
    0x19a4c116,
    0x1e376c08,
    0x2748774c,
    0x34b0bcb5,
    0x391c0cb3,
    0x4ed8aa4a,
    0x5b9cca4f,
    0x682e6ff3,
    0x748f82ee,
    0x78a5636f,
    0x84c87814,
    0x8cc70208,
    0x90befffa,
    0xa4506ceb,
    0xbef9a3f7,
    0xc67178f2,
};

static unsigned char secret[32];

#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void
sha256_block(struct sha256 *s, const unsigned char *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		    (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = s->h[0];
	b = s->h[1];
	c = s->h[2];
	d = s->h[3];
	e = s->h[4];
	f = s->h[5];
	g = s->h[6];
	h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
	s->h[5] += f;
	s->h[6] += g;
	s->h[7] += h;
}

static void
sha256_init(struct sha256 *s)
{
	static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
	    0x1f83d9ab, 0x5be0cd19};

	memcpy(s->h, iv, sizeof(iv));
	s->len = s->n = 0;
}

static void
sha256_update(struct sha256 *s, const void *data, size_t len)
{
	const unsigned char *p = data;

	s->len += len;
	while (len--) {
		s->buf[s->n++] = *p++;
		if (s->n == 64) {
			sha256_block(s, s->buf);
			s->n = 0;
		}
	}
}

static void
sha256_final(struct sha256 *s, unsigned char out[32])
{
	uint64_t bits = s->len * 8;
	unsigned char pad = 0x80, len[8];
	int i;

	sha256_update(s, &pad, 1);
	pad = 0;
	while (s->n != 56)
		sha256_update(s, &pad, 1);
	for (i = 0; i < 8; i++)
		len[i] = bits >> (56 - 8 * i);
	sha256_update(s, len, 8);
	for (i = 0; i < 32; i++)
		out[i] = s->h[i / 4] >> (24 - 8 * (i % 4));
}

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
		http_respond(c, 200, "Content-Type: image/svg+xml\r\nCache-Control: max-age=3600\r\n", FALLBACK_IMAGE,
		    sizeof(FALLBACK_IMAGE) - 1);
		return;
	}
	buf_printf(&h, "Content-Type: %s\r\nCache-Control: max-age=86400\r\n", img->mime);
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
		    fetch_url(f));
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
