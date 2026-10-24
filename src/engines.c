#include <string.h>

#include "engine.h"

extern struct engine engine_google, engine_bing, engine_google_cse, engine_brave, engine_duckduckgo,
    engine_duckduckgo_web, engine_dogpile, engine_privacywall, engine_oceanhero, engine_swisscows, engine_iseek,
    engine_s1search, engine_wikipedia, engine_duckduckgo_definitions, engine_google_images, engine_google_cse_images,
    engine_brave_images, engine_duckduckgo_images, engine_dogpile_images, engine_google_videos,
    engine_duckduckgo_videos, engine_youtube, engine_brave_videos, engine_google_news, engine_duckduckgo_news,
    engine_reuters, engine_brave_news, engine_soundcloud, engine_wikicommons_audio, engine_archlinux, engine_mdn,
    engine_mankier, engine_pypi, engine_stackoverflow, engine_askubuntu, engine_superuser, engine_arxiv,
    engine_europepmc, engine_pdbe, engine_pubmed;

static struct engine *available[] = {
    &engine_google,
    &engine_bing,
    &engine_google_cse,
    &engine_brave,
    &engine_duckduckgo,
    &engine_duckduckgo_web,
    &engine_dogpile,
    &engine_privacywall,
    &engine_oceanhero,
    &engine_swisscows,
    &engine_iseek,
    &engine_s1search,
    &engine_wikipedia,
    &engine_duckduckgo_definitions,
    &engine_google_images,
    &engine_google_cse_images,
    &engine_brave_images,
    &engine_duckduckgo_images,
    &engine_dogpile_images,
    &engine_google_videos,
    &engine_duckduckgo_videos,
    &engine_youtube,
    &engine_brave_videos,
    &engine_google_news,
    &engine_duckduckgo_news,
    &engine_reuters,
    &engine_brave_news,
    &engine_soundcloud,
    &engine_wikicommons_audio,
    &engine_archlinux,
    &engine_mdn,
    &engine_mankier,
    &engine_pypi,
    &engine_stackoverflow,
    &engine_askubuntu,
    &engine_superuser,
    &engine_arxiv,
    &engine_europepmc,
    &engine_pdbe,
    &engine_pubmed,
};

struct engine *engines[64];
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
	size_t i;

	for (i = 0; i < MAX_CATEGORIES && e->categories[i]; i++)
		if (!strcmp(e->categories[i], category))
			return 1;
	return 0;
}

/* the name as written in config.toml: "google cse" is google_cse, "brave.images" is brave_images */
static int
config_name(const struct engine *e, const char *key)
{
	const char *n = e->name;

	for (; *n && *key; n++, key++)
		if (*key != (*n == ' ' || *n == '.' ? '_' : *n))
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
			die("config.toml: unknown engine '%s'", engine_toggles[k].name);
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
