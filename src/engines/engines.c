#include <string.h>

#include "engines/engine.h"

#define ENGINE(name) extern struct engine engine_##name;
#include "engines/engines.def"
#undef ENGINE

static struct engine *available[] = {
#define ENGINE(name) &engine_##name,
#include "engines/engines.def"
#undef ENGINE
};

_Static_assert(LEN(available) <= MAX_ENGINES, "more engines than ENGINE_BIT has bits");

struct engine *engines[MAX_ENGINES];
size_t nengines;

struct engine *
engine_find(const char *name)
{
	size_t i;

	for (i = 0; i < LEN(available); i++)
		if (!strcmp(available[i]->name, name))
			return available[i];
	return NULL;
}

int
engine_in(const struct engine *e, const char *category)
{
	return !strcmp(e->category, category);
}

/* the name as written in config.toml, with underscores for its spaces: "google cse" is google_cse */
static int
config_name(const struct engine *e, const char *key)
{
	const char *n = e->name;

	for (; *n && *key; n++, key++)
		if (*key != (*n == ' ' ? '_' : *n))
			return 0;
	return !*n && !*key;
}

void
engines_init(void)
{
	struct engine *e;
	size_t i, k;

	for (k = 0; k < nengine_toggles; k++) {
		for (i = 0; i < LEN(available) && !config_name(available[i], engine_toggles[k].name); i++)
			;
		if (i == LEN(available))
			fprintf(stderr, "config.toml: unknown engine '%s' (ignored)\n", engine_toggles[k].name);
	}
	for (i = 0; i < LEN(available) && nengines < LEN(engines); i++) {
		e = available[i];
		for (k = 0; k < nengine_toggles && !config_name(e, engine_toggles[k].name); k++)
			;
		if (k < nengine_toggles) {
			if (engine_toggles[k].state == 0)
				continue;
			e->starts_off = engine_toggles[k].state == 1;
		}
		e->index = (int)nengines;
		e->enabled = 1;
		engines[nengines++] = e;
		if (e->init)
			e->init();
	}
}
