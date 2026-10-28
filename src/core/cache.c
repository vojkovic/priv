#include "core/util.h"
#include <ctype.h>
#include <errno.h>
#include <libxml/HTMLparser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <wctype.h>

struct centry {
	struct centry *next;
	char *key, *val;
	double expire;
};

static struct centry *ctable[1024];
static size_t ccount;

static uint32_t
strhash(const char *s)
{
	uint32_t h = 2166136261u;

	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

static void
cache_purge(void)
{
	struct centry **pp, *e;
	double now = now_ms();
	size_t i;

	for (i = 0; i < LEN(ctable); i++) {
		for (pp = &ctable[i]; (e = *pp);) {
			if (e->expire < now) {
				*pp = e->next;
				free(e->key);
				free(e->val);
				free(e);
				ccount--;
			} else
				pp = &e->next;
		}
	}
}

const char *
cache_get(const char *key)
{
	struct centry *e;

	for (e = ctable[strhash(key) % LEN(ctable)]; e; e = e->next)
		if (!strcmp(e->key, key))
			return e->expire >= now_ms() ? e->val : NULL;
	return NULL;
}

void
cache_set(const char *key, const char *val, int ttl)
{
	struct centry *e, **head = &ctable[strhash(key) % LEN(ctable)];

	for (e = *head; e; e = e->next) {
		if (!strcmp(e->key, key)) {
			free(e->val);
			e->val = xstrdup(val);
			e->expire = now_ms() + ttl * 1e3;
			return;
		}
	}
	if (ccount >= 4096)
		cache_purge();
	/* still full: drop this key's bucket rather than every entry */
	while (ccount >= 4096 && (e = *head)) {
		*head = e->next;
		free(e->key);
		free(e->val);
		free(e);
		ccount--;
	}
	e = xmalloc(sizeof(*e));
	e->key = xstrdup(key);
	e->val = xstrdup(val);
	e->expire = now_ms() + ttl * 1e3;
	e->next = *head;
	*head = e;
	ccount++;
}

int
mkdirs(char *p)
{
	char *s;

	for (s = p + 1; *s; s++) {
		if (*s != '/')
			continue;
		*s = 0;
		if (mkdir(p, 0700) < 0 && errno != EEXIST)
			return -1;
		*s = '/';
	}
	return mkdir(p, 0700) < 0 && errno != EEXIST ? -1 : 0;
}

const char *
cache_base(void)
{
	static char *base;
	const char *env, *parent = "/tmp", *home = "";
	size_t n;

	if (base)
		return base;
	if (cache_dir && *cache_dir)
		base = xstrdup(cache_dir);
	else {
		if ((env = getenv("XDG_CACHE_HOME")) && *env)
			parent = env;
		else if ((env = getenv("HOME")) && *env) {
			parent = env;
			home = "/.cache";
		}
		base = xmalloc((n = strlen(parent) + strlen(home) + 6));
		snprintf(base, n, "%s%s/priv", parent, home);
	}
	if (mkdirs(base) < 0)
		die("cache directory %s: %s", base, strerror(errno));
	return base;
}
