#include "botdetection/botdetect.h"
#include "engines/engine.h"
#include "features/autocomplete.h"
#include "features/favicon.h"
#include "features/proxy.h"
#include "features/stats.h"
#include "render/icons.h"
#include "render/internal.h"
#include "render/render.h"
#include "version.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *
url_authority(struct arena *a, const char *url)
{
	struct url u;

	url_parse(a, url, &u);
	return u.netloc;
}

static int
in_infobox(struct results *r, const char *url)
{
	struct infobox_url *u;
	size_t i;

	for (i = 0; url && i < r->ninfoboxes; i++)
		for (u = r->infoboxes[i]->urls; u; u = u->next)
			if (u->url && !strcmp(u->url, url))
				return 1;
	return 0;
}

static const char *
source_name(struct arena *a, const char *url)
{
	struct url u;

	url_parse(a, url, &u);
	if (strstr(u.netloc, "wikipedia.org"))
		return "Wikipedia";
	return starts_with(u.netloc, "www.") ? u.netloc + 4 : u.netloc;
}

#define SENSES_SHOWN 2

static void
senses(struct buf *b, const struct sense *s, int start, int count)
{
	buf_html(b, "<ol start=\"%d\">", start);
	for (; s && count--; s = s->next) {
		buf_html(b, "<li><span class=\"def-text\">%s</span>", s->text);
		if (s->example)
			buf_html(b, "<span class=\"def-example\">%s</span>", s->example);
		buf_puts(b, "</li>");
	}
	buf_puts(b, "</ol>");
}

static void
definition(struct buf *b, const struct definition *d)
{
	const struct word_class *c;
	const struct sense *s;
	int i;

	buf_html(b, "<h2 class=\"def-word\">%s</h2>", d->word);
	for (c = d->classes; c; c = c->next) {
		buf_html(b, "<div class=\"def-class\"><span>%s</span></div>", c->name);
		senses(b, c->senses, 1, SENSES_SHOWN);
		for (s = c->senses, i = 0; s && i < SENSES_SHOWN; s = s->next)
			i++;
		if (s) {
			buf_html(b, "<details class=\"def-more\"><summary>%s</summary>", tr("More"));
			senses(b, s, SENSES_SHOWN + 1, 8);
			buf_puts(b, "</details>");
		}
	}
}

/* a dictionary entry only fills the space when nothing else has an infobox */
struct infobox *
shown_infobox(const struct results *r)
{
	size_t i;

	for (i = 0; i < r->ninfoboxes; i++)
		if (!r->infoboxes[i]->definition)
			return r->infoboxes[i];
	return r->infoboxes[0];
}

void
infobox(struct buf *b, struct search *s, struct infobox *ib)
{
	struct infobox_url *u, *source = NULL;
	struct infobox_attr *at;
	char *img = image_url(&s->a, ib->img_src);
	int chips = 0;

	if (ib->definition) {
		buf_puts(b, "<aside class=\"infobox definition\">\n");
		definition(b, ib->definition);
		buf_html(b,
		    "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">Wiktionary" ICON_ARROW_NE "</a>\n</aside>\n",
		    ib->urls->url);
		return;
	}
	for (u = ib->urls; u && !source; u = u->next)
		if (u->title && !strcmp(u->title, "Wikipedia"))
			source = u;

	buf_puts(b, "<aside class=\"infobox\">\n");
	if (img)
		buf_html(b, "<img class=\"infobox_image\" src=\"%s\" alt=\"\">\n",
		    image_src(s, ib->img_src, img, INFOBOX_INLINE_MAX));
	buf_html(b, "<h2 class=\"title\">%s</h2>\n", ib->title);
	if (ib->description && *ib->description)
		buf_html(b, "<p class=\"description\">%s</p>\n", ib->description);
	if (ib->content && *ib->content)
		buf_html(b, "<p class=\"extract\">%s</p>\n", ib->content);

	if (ib->attrs) {
		buf_puts(b, "<dl class=\"attributes\">\n");
		for (at = ib->attrs; at; at = at->next)
			buf_html(b, "<dt>%s</dt><dd>%s</dd>\n", at->label ? at->label : "", at->value);
		buf_puts(b, "</dl>\n");
	}

	if (ib->also) {
		buf_html(b,
		    "<div class=\"also\">"
		    "<span class=\"also_label\">%s</span>"
		    "<ul>\n",
		    tr("Also refers to"));
		for (u = ib->also; u; u = u->next) {
			buf_html(b, "<li><a href=\"/search?q=%u", u->title);
			filter_params(b, &s->filters);
			buf_html(b, "\">%s</a>", u->title);
			if (u->entity)
				buf_html(b, "<span>%s</span>", u->entity);
			buf_puts(b, "</li>\n");
		}
		buf_puts(b,
		    "</ul>"
		    "</div>\n");
	}

	for (u = ib->urls; u; u = u->next) {
		if (u == source)
			continue;
		if (!chips++)
			buf_puts(b, "<div class=\"links\">");
		buf_html(b, "<a href=\"%s\" rel=\"noreferrer\">", u->url);
		favicon(b, s, url_authority(&s->a, u->url));
		buf_html(b, "%s</a>", u->title && *u->title ? u->title : u->url);
	}
	if (chips)
		buf_puts(b, "</div>\n");

	if (source)
		buf_html(b, "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">Wikipedia" ICON_ARROW_NE "</a>\n",
		    source->url);
	buf_puts(b, "</aside>\n");
}

/* answers that only repeat the infobox's links are dropped */
void
answers(struct buf *b, struct search *s)
{
	struct results *r = &s->res;
	size_t i, n;

	for (i = n = 0; i < r->nanswers; i++)
		if (!in_infobox(r, r->answers[i]->url))
			r->answers[n++] = r->answers[i];
	r->nanswers = n;
	if (!r->nanswers)
		return;
	buf_puts(b, "<div id=\"answers\">\n");
	for (i = 0; i < r->nanswers; i++) {
		buf_html(b, "<div class=\"answer\"><p>%s</p>", r->answers[i]->answer);
		if (r->answers[i]->url)
			buf_html(b, "<a class=\"source\" href=\"%s\" rel=\"noreferrer\">%s" ICON_ARROW_NE "</a>",
			    r->answers[i]->url, source_name(&s->a, r->answers[i]->url));
		buf_puts(b, "</div>\n");
	}
	buf_puts(b, "</div>\n");
}
