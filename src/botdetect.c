#include <arpa/inet.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botdetect.h"
#include "util.h"

#define BURST_WINDOW 20
#define BURST_WINDOW_MAX 15
#define BURST_WINDOW_MAX_SUSPICIOUS 2
#define LONG_WINDOW 600
#define LONG_WINDOW_MAX 150
#define LONG_WINDOW_MAX_SUSPICIOUS 10
#define SUSPICIOUS_IP_WINDOW (3600 * 24 * 30)
#define SUSPICIOUS_IP_MAX 3
#define TOKEN_LIVE_TIME 600
#define ESCALATE_TIME LONG_WINDOW
#define ESCALATE_CHILDREN 3
#define MAX_ESCALATED 1024
#define PING_LIVE_TIME 3600
#define PING_MAX 20
#define AUTOCOMPLETE_WINDOW 20
#define AUTOCOMPLETE_MAX 100
#define MAX_COUNTERS (1 << 17)

#define USER_AGENT \
	"^(unknown" \
	"|[Cc][Uu][Rr][Ll]|[wW]get|Scrapy|splash|JavaFX|FeedFetcher|python-requests|Go-http-client|Java|Jakarta|okhttp" \
	"|HttpClient|Jersey|Python|libwww-perl|Ruby|SynHttpClient|UniversalFeedParser|Googlebot|GoogleImageProxy" \
	"|bingbot|Baiduspider|yacybot|YandexMobileBot|YandexBot|Yahoo! Slurp|MJ12bot|AhrefsBot|archive.org_bot|msnbot" \
	"|MJ12bot|SeznamBot|linkdexbot|Netvibes|SMTBot|zgrab|James BOT|Sogou|Abonti|Pixray|Spinn3r|SemrushBot|Exabot" \
	"|ZmEu|BLEXBot|bitlybot|HeadlessChrome" \
	"|Mozilla/5\\.0 \\(compatible; Farside/0\\.1\\.0; \\+https://farside\\.link\\)" \
	"|.*PetalBot.*)"

struct ip {
	int v6;
	unsigned char a[16];
};

struct net {
	struct ip ip;
	int prefix;
};

struct counter {
	struct counter *next;
	char *key;
	double *ts;
	size_t n, cap;
	double last;
	int window;
};

static const char *searxng_org[] = {"167.235.158.251", "2a01:04f8:1c1c:8fc2::/64", 0};

static regex_t ua_re;
static struct net *trusted, *passlist, *blocklist;
static size_t ntrusted, npass, nblock;
static struct counter *counters[1 << 14];
static size_t ncounters;
/* ipv6 networks blocked for rate limits, so neighbouring /48s from one allocation add up to a wider block */
static struct {
	struct net net;
	double expire;
} escalated[MAX_ESCALATED];
static const int levels[] = {48, 44, 40, 36, 32, 29};
static char token[17];
static double token_expire;

static int
parse_ip(const char *s, struct ip *ip)
{
	unsigned char b[16];

	memset(ip, 0, sizeof(*ip));
	if (inet_pton(AF_INET, s, ip->a) == 1)
		return 0;
	if (inet_pton(AF_INET6, s, b) != 1)
		return -1;
	if (!memcmp(b, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12)) {
		memcpy(ip->a, b + 12, 4);
		return 0;
	}
	ip->v6 = 1;
	memcpy(ip->a, b, 16);
	return 0;
}

static int
parse_net(const char *s, struct net *n)
{
	char buf[64], *slash;

	snprintf(buf, sizeof(buf), "%s", s);
	if ((slash = strchr(buf, '/')))
		*slash = 0;
	if (parse_ip(buf, &n->ip) < 0)
		return -1;
	n->prefix = slash ? (int)parse_long(slash + 1, -1) : n->ip.v6 ? 128 : 32;
	return n->prefix < 0 || n->prefix > (n->ip.v6 ? 128 : 32) ? -1 : 0;
}

static int
in_net(const struct ip *ip, const struct net *n)
{
	int bits = n->prefix, i;

	if (ip->v6 != n->ip.v6)
		return 0;
	for (i = 0; bits > 0; i++, bits -= 8) {
		unsigned char mask = bits >= 8 ? 0xff : (unsigned char)(0xff << (8 - bits));
		if ((ip->a[i] & mask) != (n->ip.a[i] & mask))
			return 0;
	}
	return 1;
}

static struct net
masked(const struct ip *ip, int prefix)
{
	struct net n = {*ip, prefix};
	int bits = prefix, i;

	for (i = 0; i < 16; i++, bits -= 8)
		n.ip.a[i] &= bits >= 8 ? 0xff : bits > 0 ? (unsigned char)(0xff << (8 - bits)) : 0;
	return n;
}

static int
same_net(const struct net *x, const struct net *y)
{
	return x->prefix == y->prefix && !memcmp(x->ip.a, y->ip.a, sizeof(x->ip.a));
}

static int
mark_escalated(const struct net *n, double now)
{
	size_t i, slot = 0;

	for (i = 0; i < LEN(escalated); i++) {
		if (escalated[i].expire > now && same_net(&escalated[i].net, n)) {
			escalated[i].expire = now + ESCALATE_TIME * 1e3;
			return 0;
		}
		if (escalated[i].expire < escalated[slot].expire)
			slot = i;
	}
	escalated[slot].net = *n;
	escalated[slot].expire = now + ESCALATE_TIME * 1e3;
	return 1;
}

/* block the /48, then each parent once ESCALATE_CHILDREN of its direct children are blocked, up to /29 */
static void
escalate(const struct ip *ip)
{
	double now = now_ms();
	struct net parent, child;
	char addr[64];
	size_t l, i, n;

	if (!ip->v6)
		return;
	child = masked(ip, levels[0]);
	mark_escalated(&child, now);
	for (l = 1; l < LEN(levels); l++) {
		parent = masked(ip, levels[l]);
		for (i = n = 0; i < LEN(escalated); i++)
			if (escalated[i].expire > now && escalated[i].net.prefix == levels[l - 1] &&
			    in_net(&escalated[i].net.ip, &parent))
				n++;
		if (n < ESCALATE_CHILDREN)
			return;
		if (!mark_escalated(&parent, now))
			continue;
		inet_ntop(AF_INET6, parent.ip.a, addr, sizeof(addr));
		fprintf(stderr, "botdetect: BLOCK %s/%d: %zu blocked /%d networks inside\n", addr, levels[l], n,
		    levels[l - 1]);
	}
}

static int
escalated_block(const struct ip *ip)
{
	double now = now_ms();
	size_t i;

	for (i = 0; ip->v6 && i < LEN(escalated); i++)
		if (escalated[i].expire > now && escalated[i].net.prefix < levels[0] && in_net(ip, &escalated[i].net))
			return 1;
	return 0;
}

static void
load_list(const char **list, struct net **out, size_t *n, const char *name)
{
	size_t i;

	for (i = 0; list[i]; i++) {
		*out = xrealloc(*out, (*n + 1) * sizeof(**out));
		if (parse_net(list[i], &(*out)[*n]) < 0)
			fprintf(stderr, "botdetect: invalid IP %s in %s\n", list[i], name);
		else
			(*n)++;
	}
}

static int
in_list(const struct ip *ip, const struct net *list, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (in_net(ip, &list[i]))
			return 1;
	return 0;
}

static void
new_token(void)
{
	static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	size_t i;

	for (i = 0; i < sizeof(token) - 1; i++)
		token[i] = chars[rnd(sizeof(chars) - 1)];
	token[i] = 0;
	token_expire = now_ms() + TOKEN_LIVE_TIME * 1e3;
}

void
botdetect_init(void)
{
	if (regcomp(&ua_re, USER_AGENT, REG_EXTENDED | REG_NOSUB))
		die("botdetect: bad user agent pattern");
	load_list(trusted_proxies, &trusted, &ntrusted, "trusted_proxies");
	load_list(pass_ip, &passlist, &npass, "pass_ip");
	load_list(block_ip, &blocklist, &nblock, "block_ip");
	if (pass_searxng_org)
		load_list(searxng_org, &passlist, &npass, "searxng_org");
	new_token();
}

const char *
botdetect_token(void)
{
	if (now_ms() >= token_expire)
		new_token();
	return token;
}

static struct ip
real_ip(struct request *r)
{
	struct ip peer, ip, xff[32];
	char buf[1024], *item, *save;
	int have_peer, nxff = 0, i;

	have_peer = parse_ip(r->remote_addr, &peer) == 0;
	if ((*r->x_forwarded_for || *r->x_real_ip) && have_peer && in_list(&peer, trusted, ntrusted)) {
		snprintf(buf, sizeof(buf), "%s", r->x_forwarded_for);
		for (item = strtok_r(buf, ",", &save); item && nxff < (int)LEN(xff);
		    item = strtok_r(NULL, ",", &save)) {
			if (parse_ip(str_strip(item), &xff[nxff]) < 0) {
				nxff = 0;
				break;
			}
			nxff++;
		}
		for (i = nxff - 1; i >= 0; i--)
			if (!in_list(&xff[i], trusted, ntrusted))
				return xff[i];
		if (nxff)
			return xff[0];
		snprintf(buf, sizeof(buf), "%s", r->x_real_ip);
		if (parse_ip(str_strip(buf), &ip) == 0)
			return ip;
	}
	if (have_peer)
		return peer;
	parse_ip("100::", &ip);
	return ip;
}

const char *
botdetect_client_ip(struct request *r)
{
	static char addr[INET6_ADDRSTRLEN];
	struct ip ip = real_ip(r);

	inet_ntop(ip.v6 ? AF_INET6 : AF_INET, ip.a, addr, sizeof(addr));
	return addr;
}

static void
network(const struct ip *ip, char *out, size_t len)
{
	struct net n = {*ip, ip->v6 ? ipv6_prefix : ipv4_prefix};
	char addr[64];
	int bits = n.prefix, i;

	for (i = 0; i < 16; i++, bits -= 8)
		n.ip.a[i] &= bits >= 8 ? 0xff : bits > 0 ? (unsigned char)(0xff << (8 - bits)) : 0;
	inet_ntop(n.ip.v6 ? AF_INET6 : AF_INET, n.ip.a, addr, sizeof(addr));
	snprintf(out, len, "%s/%d", addr, n.prefix);
}

static int
link_local(const struct ip *ip)
{
	return ip->v6 ? ip->a[0] == 0xfe && (ip->a[1] & 0xc0) == 0x80 : ip->a[0] == 169 && ip->a[1] == 254;
}

static uint32_t
hash(const char *s)
{
	uint32_t h = 2166136261u;

	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

static void
purge(double now)
{
	struct counter **pp, *c;
	size_t i;

	for (i = 0; i < LEN(counters); i++) {
		for (pp = &counters[i]; (c = *pp);) {
			if (now - c->last > c->window * 1e3) {
				*pp = c->next;
				free(c->key);
				free(c->ts);
				free(c);
				ncounters--;
			} else
				pp = &c->next;
		}
	}
}

static struct counter **
slot(const char *key)
{
	struct counter **pp;

	for (pp = &counters[hash(key) % LEN(counters)]; *pp; pp = &(*pp)->next)
		if (!strcmp((*pp)->key, key))
			break;
	return pp;
}

static void
evict_bucket(const char *key)
{
	struct counter **head = &counters[hash(key) % LEN(counters)], *c;

	while ((c = *head)) {
		*head = c->next;
		free(c->key);
		free(c->ts);
		free(c);
		ncounters--;
	}
}

static size_t
incr_sliding_window(const char *name, const char *net, int window)
{
	static double last_purge;
	struct counter **pp, *c;
	double now = now_ms();
	char key[160];
	size_t i, j;

	if (now - last_purge > 60e3) {
		purge(now);
		last_purge = now;
	}
	snprintf(key, sizeof(key), "%s:%s", name, net);
	pp = slot(key);
	if (!*pp && ncounters >= MAX_COUNTERS) {
		/* a flood of networks evicts its own bucket instead of growing without bound */
		evict_bucket(key);
		pp = slot(key);
	}
	if (!(c = *pp)) {
		ncounters++;
		c = *pp = xcalloc(1, sizeof(*c));
		c->key = xstrdup(key);
		c->window = window;
	}
	for (i = j = 0; i < c->n; i++)
		if (now - c->ts[i] <= window * 1e3)
			c->ts[j++] = c->ts[i];
	c->n = j;
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 8;
		c->ts = xrealloc(c->ts, c->cap * sizeof(*c->ts));
	}
	c->ts[c->n++] = now;
	c->last = now;
	return c->n;
}

static void
drop_counter(const char *name, const char *net)
{
	struct counter **pp, *c;
	char key[160];

	snprintf(key, sizeof(key), "%s:%s", name, net);
	pp = slot(key);
	if ((c = *pp)) {
		*pp = c->next;
		free(c->key);
		free(c->ts);
		free(c);
		ncounters--;
	}
}

static const char *
ping_key(const char *net, struct request *r)
{
	static char key[1024];

	snprintf(key, sizeof(key), "botdetect ping|%s|%s|%s", net, r->accept_language, r->user_agent);
	return key;
}

void
botdetect_ping(struct conn *c, struct request *r, const char *tok)
{
	struct ip ip = real_ip(r);
	char net[96];

	network(&ip, net, sizeof(net));
	/* the token is public, so one network could otherwise push every other client's ping out */
	if (!strcmp(tok, botdetect_token())) {
		const char *key = ping_key(net, r);

		if (cache_get(key))
			cache_set(key, "1", PING_LIVE_TIME);
		else if (incr_sliding_window("PING", net, PING_LIVE_TIME) <= PING_MAX)
			cache_set(key, "1", PING_LIVE_TIME);
	}
	http_respond(c, 200, "Content-Type: text/css\r\nCache-Control: no-store\r\n", "", 0);
}

static int
too_many(struct conn *c, const char *net, const char *why)
{
	fprintf(stderr, "botdetect: BLOCK %s: %s\n", net, why);
	http_respond(c, 429, "Content-Type: text/plain\r\n", "Too Many Requests", 17);
	return 1;
}

static int
redirect_index(struct conn *c)
{
	http_respond(c, 302, "Location: /\r\nCache-Control: no-store, max-age=0\r\n", "", 0);
	return 1;
}

static int
accepts_html(const char *accept)
{
	char buf[1024], *item, *save, *semi;

	snprintf(buf, sizeof(buf), "%s", accept);
	for (item = strtok_r(buf, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
		if ((semi = strchr(item, ';')))
			*semi = 0;
		item = str_strip(item);
		if (!strcmp(item, "text/html") || !strcmp(item, "text/*") || !strcmp(item, "*/*"))
			return 1;
	}
	return 0;
}

static int
accepts_gzip(const char *enc)
{
	char buf[512], *item, *save;

	snprintf(buf, sizeof(buf), "%s", enc);
	for (item = strtok_r(buf, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
		item = str_strip(item);
		if (!strcmp(item, "gzip") || !strcmp(item, "deflate"))
			return 1;
	}
	return 0;
}

static int
version_after(const char *ua, const char *needle, int min)
{
	const char *p = strcasestr(ua, needle);

	return p && IS_DIGIT(p[strlen(needle)]) ? strtol(p + strlen(needle), NULL, 10) >= min : -1;
}

static int
browser_supported(const char *ua)
{
	const char *p;
	char *end;
	long major, minor;
	int r;

	if ((r = version_after(ua, "chrome/", 80)) >= 0)
		return r;
	if ((r = version_after(ua, "firefox/", 90)) >= 0)
		return r;
	if ((p = strcasestr(ua, "version/")) && IS_DIGIT(p[8])) {
		major = strtol(p + 8, &end, 10);
		if (*end == '.' && IS_DIGIT(end[1])) {
			minor = strtol(end + 1, NULL, 10);
			return major > 16 || (major == 16 && minor >= 4);
		}
	}
	return 0;
}

static int
blank(const char *s)
{
	while (IS_SPACE(*s))
		s++;
	return !*s;
}

static int
bad_user_agent(struct request *r)
{
	return !regexec(&ua_re, r->has_user_agent ? r->user_agent : "unknown", 0, NULL, 0);
}

static int
limited(struct conn *c, const struct ip *ip, const char *net, const char *why)
{
	too_many(c, net, why);
	escalate(ip);
	return 1;
}

static int
ip_limit(struct conn *c, const struct ip *ip, const char *net, struct request *r)
{
	char pk[1024];

	if (link_local(ip) && !limiter_filter_link_local)
		return 0;
	if (escalated_block(ip)) {
		http_respond(c, 429, "Content-Type: text/plain\r\n", "Too Many Requests", 17);
		return 1;
	}
	if (limiter_link_token) {
		snprintf(pk, sizeof(pk), "%s", ping_key(net, r));
		if (cache_get(pk)) {
			cache_set(pk, "1", PING_LIVE_TIME);
			drop_counter("SUSPICIOUS_IP_WINDOW", net);
			return 0;
		}
		if (incr_sliding_window("SUSPICIOUS_IP_WINDOW", net, SUSPICIOUS_IP_WINDOW) > SUSPICIOUS_IP_MAX) {
			fprintf(stderr,
			    "botdetect: BLOCK %s: too many request in SUSPICIOUS_IP_WINDOW (redirect to /)\n", net);
			return redirect_index(c);
		}
		if (incr_sliding_window("BURST_WINDOW", net, BURST_WINDOW) > BURST_WINDOW_MAX_SUSPICIOUS)
			return limited(c, ip, net, "too many request in BURST_WINDOW (BURST_WINDOW_MAX_SUSPICIOUS)");
		if (incr_sliding_window("LONG_WINDOW", net, LONG_WINDOW) > LONG_WINDOW_MAX_SUSPICIOUS)
			return limited(c, ip, net, "too many request in LONG_WINDOW (LONG_WINDOW_MAX_SUSPICIOUS)");
		return 0;
	}
	if (incr_sliding_window("BURST_WINDOW", net, BURST_WINDOW) > BURST_WINDOW_MAX)
		return limited(c, ip, net, "too many request in BURST_WINDOW (BURST_WINDOW_MAX)");
	if (incr_sliding_window("LONG_WINDOW", net, LONG_WINDOW) > LONG_WINDOW_MAX)
		return limited(c, ip, net, "too many request in LONG_WINDOW (LONG_WINDOW_MAX)");
	return 0;
}

int
botdetect(struct conn *c, struct request *r)
{
	struct ip ip;
	char net[96];

	if (!limiter)
		return 0;
	ip = real_ip(r);
	network(&ip, net, sizeof(net));
	if (link_local(&ip) || in_list(&ip, passlist, npass))
		return 0;
	if (in_list(&ip, blocklist, nblock)) {
		fprintf(stderr, "botdetect: BLOCK %s: matched BLOCKLIST\n", net);
		http_respond(c, 429, "Content-Type: text/plain\r\n", "IP is on BLOCKLIST", 18);
		return 1;
	}
	if (bad_user_agent(r))
		return too_many(c, net, "bot detected, HTTP header User-Agent");
	/* every suggestion is a request to google from this server */
	if (!strcmp(r->path, "/autocompleter") &&
	    incr_sliding_window("AUTOCOMPLETE_WINDOW", net, AUTOCOMPLETE_WINDOW) > AUTOCOMPLETE_MAX)
		return too_many(c, net, "too many request in AUTOCOMPLETE_WINDOW");
	if (strcmp(r->path, "/search"))
		return 0;
	if (!r->has_accept || !accepts_html(r->accept))
		return too_many(c, net, "HTTP header Accept did not contain text/html");
	if (!accepts_gzip(r->accept_encoding))
		return too_many(c, net, "HTTP header Accept-Encoding did not contain gzip nor deflate");
	if (blank(r->accept_language))
		return too_many(c, net, "missing HTTP header Accept-Language");
	if (!strcmp(r->proto, "https") && browser_supported(r->user_agent) && strcmp(r->sec_fetch_mode, "navigate") &&
	    strcmp(r->sec_fetch_mode, "cors"))
		return redirect_index(c);
	return ip_limit(c, &ip, net, r);
}
