#ifndef UTIL_H
#define UTIL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define IS_SPACE(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))

/* 0 leaves an engine out, 1 offers it switched off, 2 offers it switched on */
struct engine_toggle {
	const char *name;
	int state;
};

extern const char *instance_name;
extern const char *contact_url;
extern const char **motd;
extern const char *listen_addr;
extern int listen_port;
extern const char *cache_dir;
extern int favicon_hold_days;
extern size_t favicon_cache_bytes;
extern int limiter, limiter_link_token, limiter_filter_link_local, ipv4_prefix, ipv6_prefix;
extern const char **trusted_proxies, *pass_ip[], *block_ip[];
extern int ban_time_on_fail, max_ban_time_on_fail;
extern int suspend_access_denied, suspend_captcha, suspend_too_many_requests;
extern int suspend_cf_captcha, suspend_cf_access_denied, suspend_recaptcha;
extern const char *impersonate_chrome, *impersonate_firefox;
extern const char *useragent_os[], *useragent_versions[];
extern struct engine_toggle *engine_toggles;
extern size_t nengine_toggles;

const char *config_load(void);

struct buf {
	char *p;
	size_t len, cap;
};

void buf_grow(struct buf *b, size_t n);
void buf_add(struct buf *b, const void *p, size_t n);
void buf_puts(struct buf *b, const char *s);
void buf_putc(struct buf *b, char c);
void buf_printf(struct buf *b, const char *fmt, ...);
void buf_vprintf(struct buf *b, const char *fmt, va_list ap);
void buf_html(struct buf *b, const char *fmt, ...);
const char *buf_str(struct buf *b);
void buf_free(struct buf *b);

struct arena {
	struct chunk *head;
};

void *amalloc(struct arena *a, size_t n);
char *astrdup(struct arena *a, const char *s);
char *astrndup(struct arena *a, const char *s, size_t n);
char *aprintf(struct arena *a, const char *fmt, ...);
void arena_free(struct arena *a);

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
_Noreturn void die(const char *fmt, ...);

double now_ms(void);
double wall_time(void);
uint32_t rnd(uint32_t n);

void html_escape(struct buf *b, const char *s);
void url_quote(struct buf *b, const char *s, const char *safe, int plus);
void form_add(struct buf *b, const char *key, const char *val);
char *url_decode(char *s);
const char *query_param(struct arena *a, const char *qs, const char *key);

struct url {
	char *scheme, *netloc, *path, *params, *query, *fragment;
};

int url_parse(struct arena *a, const char *s, struct url *u);
char *url_unparse(struct arena *a, const struct url *u);
char *url_join(struct arena *a, const char *base, const char *ref);
char *url_query_get(struct arena *a, const char *url, const char *key);
char *idna_decode(struct arena *a, const char *host);

int starts_with(const char *s, const char *prefix);
int web_url(const char *s);
long parse_long(const char *s, long fallback);
const char *find_between(const char *s, const char *begin, const char *end, size_t *n);
char *str_strip(char *s);
char *py_strip(char *s);
char *text_split_join(struct arena *a, const char *s);
char *text_normalize(struct arena *a, const char *s, size_t max);
char *html_to_text(struct arena *a, const char *s);
char *base64(struct arena *a, const unsigned char *p, size_t n);

struct sha256 {
	uint32_t h[8];
	uint64_t len;
	unsigned char buf[64];
	size_t n;
};
void sha256_init(struct sha256 *s);
void sha256_update(struct sha256 *s, const void *data, size_t len);
void sha256_final(struct sha256 *s, unsigned char out[32]);
size_t utf8_len(const char *s);
int utf8_decode(const char *s, uint32_t *cp);
int utf8_encode(char *out, uint32_t cp);
int is_py_space(uint32_t cp);
char *py_title(struct arena *a, const char *s);
int py_islower(const char *s);
char *gen_useragent(void);

const char *cache_base(void);
int mkdirs(char *p);
const char *cache_get(const char *key);
void cache_set(const char *key, const char *val, int ttl);

/* svg must never run script */
#define MEDIA_HEADERS \
	"Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; sandbox\r\n" \
	"X-Content-Type-Options: nosniff\r\n"

#endif
