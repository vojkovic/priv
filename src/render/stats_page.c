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

struct stats_row {
	const struct engine *e;
	int reliability, suspended;
	double typical;
	const char *error;
};

static int
float_cmp(const void *x, const void *y)
{
	float a = *(const float *)x, b = *(const float *)y;

	return a < b ? -1 : a > b;
}

static int
stats_cmp(const void *x, const void *y)
{
	return strcmp(((const struct stats_row *)x)->e->name, ((const struct stats_row *)y)->e->name);
}

/* reliability is the share of recent searches the engine answered, typical its median time for those */
static void
stats_row(struct stats_row *row, const struct engine *e)
{
	const struct engine_stats *st = stats_of(e);
	float times[STATS_WINDOW] = {0};
	size_t k, n = 0;

	row->e = e;
	row->suspended = e->suspend_end > now_ms();
	row->reliability = -1;
	row->typical = 0;
	row->error = NULL;
	if (!st)
		return;
	for (k = 0; k < st->nrecent; k++)
		if (st->recent[k].outcome == OUTCOME_OK || st->recent[k].outcome == OUTCOME_EMPTY)
			times[n++] = st->recent[k].seconds;
	qsort(times, n, sizeof(*times), float_cmp);
	if (st->nrecent)
		row->reliability = (int)(n * 100 / st->nrecent);
	row->typical = n ? times[n / 2] : 0;
	row->error = *st->error ? st->error : NULL;
}

void
render_stats(struct buf *b, const struct prefs *p)
{
	struct stats_row *rows, *r;
	const char *cls;
	size_t i;

	page_start(b, p, "Engine stats", "Engine stats");
	rows = amalloc(&scratch, (nengines + 1) * sizeof(*rows));
	for (i = 0; i < nengines; i++)
		stats_row(&rows[i], engines[i]);
	qsort(rows, nengines, sizeof(*rows), stats_cmp);

	buf_puts(b,
	    "<div class=\"stats\">"
	    "<table>\n"
	    "<thead><tr><th>Engine</th><th>Reliability</th><th>Response time</th><th>Last error</th></tr></thead>"
	    "<tbody>\n");
	for (i = 0; i < nengines; i++) {
		r = &rows[i];
		cls = r->suspended || (r->reliability >= 0 && r->reliability < 50) ? "bad" :
		    r->reliability >= 0 && r->reliability < 85                     ? "warn" :
		                                                                     "";
		buf_html(b,
		    "<tr>"
		    "<td><img src=\"%s\" alt=\"\" loading=\"lazy\">%s</td>"
		    "<td class=\"%r\">",
		    favicon_url(&scratch, r->e->website, p->favicon), r->e->name, cls);
		if (r->suspended)
			buf_puts(b, "suspended");
		else if (r->reliability >= 0)
			buf_printf(b, "%d%%", r->reliability);
		buf_puts(b, "</td><td>");
		if (r->typical > 0)
			buf_printf(b, "%.2fs", r->typical);
		buf_html(b,
		    "</td>"
		    "<td>%s</td>"
		    "</tr>\n",
		    r->suspended ? r->e->suspend_reason :
		        r->error ? r->error :
		                   "");
	}
	buf_puts(b,
	    "</tbody>"
	    "</table>"
	    "</div>\n"
	    "</main>\n"
	    "</body>\n"
	    "</html>\n");
	arena_free(&scratch);
}
