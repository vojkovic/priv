#ifndef JS_H
#define JS_H

#include <stddef.h>

#include "../lib/yyjson/yyjson.h"

#define JS_NOMATCH ((size_t)-1)

struct js_env {
	size_t n;
	const char **names;
	size_t *lens;
	yyjson_mut_val **vals;
};

size_t js_match(const char *s, size_t len, size_t start, char open, char close);
yyjson_mut_val *js_parse(yyjson_mut_doc *d, const char *s, size_t len, size_t *pos, const struct js_env *env);
yyjson_mut_val *js_page_data(yyjson_mut_doc *d, const char *text);

#endif
