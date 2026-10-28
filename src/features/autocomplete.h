#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include <stddef.h>

#include "core/http.h"

#define AUTOCOMPLETE_DEFAULT "google"

/* the suggestion services offered on /preferences; "off" asks none */
struct autocompleter {
	const char *name, *label;
};

extern const struct autocompleter autocompleters[];
extern const size_t nautocompleters;

void autocomplete(struct conn *c, struct request *r);

#endif
