#include <string.h>

#include "engine.h"

extern struct engine engine_google, engine_google_cse, engine_brave, engine_duckduckgo, engine_duckduckgo_web,
    engine_dogpile, engine_privacywall, engine_wikipedia, engine_duckduckgo_definitions, engine_google_images,
    engine_google_cse_images, engine_brave_images, engine_duckduckgo_images, engine_dogpile_images,
    engine_google_videos, engine_duckduckgo_videos, engine_youtube, engine_brave_videos, engine_google_news,
    engine_duckduckgo_news, engine_reuters, engine_brave_news, engine_soundcloud, engine_wikicommons_audio,
    engine_archlinux, engine_mdn, engine_mankier, engine_pypi, engine_stackoverflow, engine_askubuntu, engine_superuser,
    engine_arxiv, engine_europepmc, engine_pdbe, engine_pubmed;

static struct engine *available[] = {
    &engine_google,
    &engine_google_cse,
    &engine_brave,
    &engine_duckduckgo,
    &engine_duckduckgo_web,
    &engine_dogpile,
    &engine_privacywall,
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

void
engines_init(void)
{
	struct engine_setting *s;
	struct engine *e;

	for (s = engine_settings; s->name; s++) {
		if (!(e = engine_find(s->name)))
			die("config.h: unknown engine '%s'", s->name);
		if (!s->enabled || nengines == LEN(engines))
			continue;
		e->index = (int)nengines;
		e->enabled = 1;
		e->weight = s->weight;
		engines[nengines++] = e;
		if (e->init)
			e->init();
	}
}
