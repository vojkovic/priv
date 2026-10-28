#ifndef LOCALES_H
#define LOCALES_H

#include <stddef.h>

struct locale {
	const char *tag, *label, *names, *accept_language;
	const char *google_hl, *google_lr, *google_cr, *google_gl;
	const char *brave_country, *brave_ui_lang;
	const char *ddg_region, *ddg_lang;
	const char *privacywall_cc;
	const char *wiki_netloc;
};

extern const struct locale locales[];
extern const size_t nlocales;

const struct locale *locale_find(const char *tag);

#endif
