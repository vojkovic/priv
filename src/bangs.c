#include <string.h>
#include <strings.h>

#include "bangs.h"
#include "engine.h"

static const struct engine *
bang_find(const char *name)
{
	size_t i;

	for (i = 0; i < nengines; i++)
		if (engines[i]->bang && !strcasecmp(engines[i]->shortcut, name))
			return engines[i];
	return NULL;
}

const char *
bang_redirect(struct arena *a, const char *q)
{
	char *words = astrdup(a, q), *w, *save;
	struct buf rest = {0}, url = {0};
	const struct engine *e = NULL;
	const char *out, *at;

	for (w = strtok_r(words, " \t\n\r", &save); w; w = strtok_r(NULL, " \t\n\r", &save)) {
		if (!e && w[0] == '!' && w[1] == '!' && (e = bang_find(w + 2)))
			continue;
		if (rest.len)
			buf_putc(&rest, ' ');
		buf_puts(&rest, w);
	}
	if (!e) {
		buf_free(&rest);
		return NULL;
	}
	if (rest.len) {
		at = strstr(e->bang, "%s");
		buf_add(&url, e->bang, at - e->bang);
		url_quote(&url, buf_str(&rest), "", 1);
		buf_puts(&url, at + 2);
	} else {
		buf_puts(&url, "https://");
		buf_puts(&url, e->website);
	}
	out = astrdup(a, buf_str(&url));
	buf_free(&rest);
	buf_free(&url);
	return out;
}
