#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static const char *paths[] = {"config.toml", "/etc/privy/config.toml"};

/* settings from config.toml */
const char *instance_name = "privy";
const char **motd;
const char *listen_addr = "::1";
int listen_port = 8888;
double search_timeout = 2.0;
int limiter = 1;
int limiter_link_token = 0;
static const char *default_proxies[] = {"127.0.0.0/8", "::1", NULL};
const char **trusted_proxies = default_proxies;
struct engine_toggle *engine_toggles;
size_t nengine_toggles;

/* fixed defaults */
const char *cache_dir = NULL;
int favicon_hold_days = 30;
size_t favicon_cache_bytes = 50 << 20;
int limiter_filter_link_local = 0;
int ipv4_prefix = 24;
int ipv6_prefix = 48;
const char *pass_ip[] = {NULL};
const char *block_ip[] = {NULL};
int pass_searxng_org = 1;
int ban_time_on_fail = 5;
int max_ban_time_on_fail = 120;
int suspend_access_denied = 180;
int suspend_captcha = 3600;
int suspend_too_many_requests = 180;
int suspend_cf_captcha = 1296000;
int suspend_cf_access_denied = 86400;
int suspend_recaptcha = 604800;
const char *impersonate_chrome = "chrome150";
const char *impersonate_firefox = "firefox147";
const char *useragent_os[] = {"Windows NT 10.0; Win64; x64", "X11; Linux x86_64", NULL};
const char *useragent_versions[] = {"157.0", "156.0", NULL};

enum type { STRING, BOOL, NUMBER, ARRAY };

struct value {
	enum type type;
	char *s;
	double n;
	int b;
	const char **list;
};

static struct arena scratch;

struct parser {
	const char *path;
	int line;
	char *p;
};

_Noreturn static void
fail(struct parser *ps, const char *msg)
{
	die("%s:%d: %s", ps->path, ps->line, msg);
}

static void
skip_space(struct parser *ps)
{
	while (*ps->p == ' ' || *ps->p == '\t')
		ps->p++;
}

static char *
quoted(struct parser *ps)
{
	struct buf b = {0};
	char *out;

	for (ps->p++; *ps->p != '"'; ps->p++) {
		if (!*ps->p || *ps->p == '\n')
			fail(ps, "unterminated string");
		if (*ps->p == '\\' && (ps->p[1] == '"' || ps->p[1] == '\\'))
			ps->p++;
		buf_putc(&b, *ps->p);
	}
	ps->p++;
	out = xstrdup(buf_str(&b));
	buf_free(&b);
	return out;
}

static char *
key(struct parser *ps)
{
	const char *start = ps->p;

	if (*ps->p == '"')
		return quoted(ps);
	while (IS_ALNUM(*ps->p) || *ps->p == '_' || *ps->p == '-' || *ps->p == '.')
		ps->p++;
	if (ps->p == start)
		fail(ps, "expected a key");
	return strndup(start, ps->p - start);
}

static void
value(struct parser *ps, struct value *v)
{
	char *end;
	size_t n = 0;

	memset(v, 0, sizeof(*v));
	if (*ps->p == '"') {
		v->type = STRING;
		v->s = quoted(ps);
	} else if (!strncmp(ps->p, "true", 4) || !strncmp(ps->p, "false", 5)) {
		v->type = BOOL;
		v->b = *ps->p == 't';
		ps->p += v->b ? 4 : 5;
	} else if (*ps->p == '[') {
		v->type = ARRAY;
		v->list = xcalloc(1, sizeof(*v->list));
		for (ps->p++;;) {
			skip_space(ps);
			if (*ps->p == ']')
				break;
			if (*ps->p != '"')
				fail(ps, "arrays can only hold strings");
			v->list = xrealloc(v->list, (n + 2) * sizeof(*v->list));
			v->list[n++] = quoted(ps);
			v->list[n] = NULL;
			skip_space(ps);
			if (*ps->p == ',')
				ps->p++;
			else if (*ps->p != ']')
				fail(ps, "expected , or ] in array");
		}
		ps->p++;
	} else {
		v->type = NUMBER;
		v->n = strtod(ps->p, &end);
		if (end == ps->p)
			fail(ps, "expected a string, true/false, a number or [ ... ]");
		ps->p = end;
	}
}

static void
expect(struct parser *ps, const struct value *v, enum type type)
{
	static const char *names[] = {"a string", "true or false", "a number", "a list of strings"};

	if (v->type != type)
		fail(ps, aprintf(&scratch, "expected %s", names[type]));
}

static void
set(struct parser *ps, const char *section, const char *k, struct value *v)
{
	if (!*section && !strcmp(k, "instance_name")) {
		expect(ps, v, STRING);
		instance_name = v->s;
	} else if (!*section && !strcmp(k, "motd")) {
		if (v->type == STRING) {
			motd = xcalloc(2, sizeof(*motd));
			motd[0] = *v->s ? v->s : NULL;
		} else {
			expect(ps, v, ARRAY);
			motd = v->list;
		}
	} else if (!*section && !strcmp(k, "listen")) {
		expect(ps, v, STRING);
		listen_addr = v->s;
	} else if (!*section && !strcmp(k, "port")) {
		expect(ps, v, NUMBER);
		listen_port = (int)v->n;
	} else if (!*section && !strcmp(k, "timeout")) {
		expect(ps, v, NUMBER);
		search_timeout = v->n;
	} else if (!strcmp(section, "limiter") && !strcmp(k, "enabled")) {
		expect(ps, v, BOOL);
		limiter = v->b;
	} else if (!strcmp(section, "limiter") && !strcmp(k, "link_token")) {
		expect(ps, v, BOOL);
		limiter_link_token = v->b;
	} else if (!strcmp(section, "limiter") && !strcmp(k, "trusted_proxies")) {
		expect(ps, v, ARRAY);
		trusted_proxies = v->list;
	} else if (!strcmp(section, "engines")) {
		expect(ps, v, NUMBER);
		if (v->n < 0)
			fail(ps, "an engine's weight can't be negative");
		engine_toggles = xrealloc(engine_toggles, (nengine_toggles + 1) * sizeof(*engine_toggles));
		engine_toggles[nengine_toggles].name = xstrdup(k);
		engine_toggles[nengine_toggles++].weight = v->n;
	} else {
		fail(ps, aprintf(&scratch, "unknown setting %s%s%s", section, *section ? "." : "", k));
	}
}

static void
parse(const char *path, char *text)
{
	struct parser ps = {path, 1, text};
	char *section = xstrdup(""), *k;
	struct value v;

	for (; *ps.p; ps.line++) {
		skip_space(&ps);
		if (*ps.p == '[') {
			ps.p++;
			skip_space(&ps);
			free(section);
			section = key(&ps);
			skip_space(&ps);
			if (*ps.p++ != ']')
				fail(&ps, "expected ]");
		} else if (*ps.p && *ps.p != '#' && *ps.p != '\n' && *ps.p != '\r') {
			k = key(&ps);
			skip_space(&ps);
			if (*ps.p++ != '=')
				fail(&ps, "expected =");
			skip_space(&ps);
			value(&ps, &v);
			set(&ps, section, k, &v);
			free(k);
		}
		skip_space(&ps);
		if (*ps.p == '#')
			ps.p += strcspn(ps.p, "\n");
		if (*ps.p == '\r')
			ps.p++;
		if (*ps.p && *ps.p != '\n')
			fail(&ps, "unexpected text after the value");
		if (*ps.p)
			ps.p++;
	}
	free(section);
}

const char *
config_load(void)
{
	struct buf b = {0};
	char chunk[4096];
	size_t i, n;
	FILE *fp = NULL;

	for (i = 0; i < LEN(paths) && !fp; i++)
		fp = fopen(paths[i], "r");
	if (!fp)
		return NULL;
	while (!feof(fp) && !ferror(fp) && (n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
		buf_add(&b, chunk, n);
	fclose(fp);
	parse(paths[i - 1], (char *)buf_str(&b));
	buf_free(&b);
	return paths[i - 1];
}
