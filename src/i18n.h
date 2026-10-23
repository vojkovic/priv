#ifndef I18N_H
#define I18N_H

#include <stddef.h>

struct ui_lang {
	const char *code, *tag, *name;
	int rtl;
	const char *const *msgs;
};

extern const struct ui_lang ui_langs[];
extern const size_t nui_langs;
extern const char *const msgids[];
extern const size_t nmsgids;

#endif
