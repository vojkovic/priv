#ifndef PREFS_H
#define PREFS_H

#include <stdint.h>

#include "http.h"
#include "i18n.h"
#include "util.h"

#define PREFS_CATEGORIES 16
#define SAFESEARCH_DEFAULT 1
#define THEME_DEFAULT "macchiato"

struct engine;

/* the colour themes in static/style.css, by the name its data-theme selectors use */
struct theme {
	const char *name, *label;
};

extern const struct theme themes[];
extern const size_t nthemes;

/* what a user chose on /preferences, kept only in their own cookie */
struct prefs {
	const struct ui_lang *ui, *ui_auto;
	const char *ui_code, *language, *theme, *favicon, *autocomplete;
	int safesearch, saved;
	uint64_t off[PREFS_CATEGORIES];
};

void prefs_parse(struct arena *a, struct request *r, struct prefs *p);
void prefs_from_form(struct arena *a, struct request *r, struct prefs *p);
char *prefs_cookie(struct arena *a, const struct prefs *p, int secure);
int prefs_engine_on(const struct prefs *p, const char *category, const struct engine *e);

void i18n_use(const struct ui_lang *l);
const char *tr(const char *msgid);

#endif
