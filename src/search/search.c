#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engines/engine.h"
#include "features/favicon.h"
#include "features/metrics.h"
#include "features/proxy.h"
#include "features/stats.h"
#include "render/icons.h"
#include "render/render.h"
#include "search/search.h"

#define PREFETCH_THUMBNAILS 10
#define PREFETCH_IMAGES 6

#define STREAM_HEADERS \
	"Content-Type: text/html; charset=utf-8\r\n" \
	"Cache-Control: private, no-cache\r\n" \
	"X-Accel-Buffering: no\r\n" \
	"Trailer: Server-Timing\r\n" SECURITY_HEADERS

static void
job_cleanup(struct job *j)
{
	int i;

	if (j->f) {
		fetch_free(j->f);
		j->f = NULL;
	}
	for (i = j->ncleanup - 1; i >= 0; i--)
		j->cleanup[i].fn(j->cleanup[i].p);
	j->ncleanup = 0;
}

static void
search_free(struct search *s)
{
	size_t i;

	ev_timer_stop(&s->deadline);
	for (i = 0; i < s->njobs; i++)
		job_cleanup(&s->jobs[i]);
	free(s->res.main);
	free(s->res.infoboxes);
	free(s->res.answers);
	free(s->res.unresponsive);
	free(s->res.timings);
	free(s->jobs);
	arena_free(&s->a);
	free(s);
}

static int
by_total(const void *x, const void *y)
{
	const struct timing *a = x, *b = y;

	return (a->seconds > b->seconds) - (a->seconds < b->seconds);
}

static const char *
token(struct arena *a, const char *name)
{
	char *t = astrdup(a, name), *p;

	for (p = t; *p; p++)
		if (!IS_ALNUM(*p) && *p != '.' && *p != '-')
			*p = '_';
	return t;
}

/* searxng's Server-Timing, sent as a trailer since the head is streamed before the engines finish */
static const char *
server_timing(struct search *s)
{
	struct results *r = &s->res;
	struct buf b = {0};
	const char *out;
	size_t i;

	if (r->ntimings)
		qsort(r->timings, r->ntimings, sizeof(*r->timings), by_total);
	buf_printf(&b, "Server-Timing: total;dur=%.3f", now_ms() - s->start);
	for (i = 0; i < r->ntimings; i++)
		buf_printf(&b, ", total_%zu_%s;dur=%.3f", i, token(&s->a, r->timings[i].engine->name),
		    r->timings[i].seconds * 1e3);
	for (i = 0; i < r->ntimings; i++)
		if (r->timings[i].load > 0)
			buf_printf(&b, ", load_%zu_%s;dur=%.3f", i, token(&s->a, r->timings[i].engine->name),
			    r->timings[i].load * 1e3);
	buf_puts(&b, "\r\n");
	out = astrdup(&s->a, buf_str(&b));
	buf_free(&b);
	return out;
}

static void
search_finish(struct search *s)
{
	struct buf b = {0};
	size_t i;

	ev_timer_stop(&s->deadline);

	//
	// Give up on the stragglers
	//
	// anything still running now timed out, cancel its request and say so
	//
	for (i = 0; i < s->njobs; i++) {
		if (s->jobs[i].done)
			continue;
		s->jobs[i].done = 1;
		if (s->jobs[i].f)
			metrics_cancelled(s->jobs[i].e);
		job_cleanup(&s->jobs[i]);
		results_unresponsive(&s->res, s->jobs[i].e, "timeout", 0);
		results_timing(&s->res, s->jobs[i].e, (now_ms() - s->start) / 1e3, s->jobs[i].load);
		stats_record(s->jobs[i].e, OUTCOME_TIMEOUT, (now_ms() - s->start) / 1e3, "timeout");
	}
	s->elapsed = (now_ms() - s->start) / 1e3;

	//
	// Merge, rank, render
	//
	results_close(&s->res);
	render_results(&b, s);

	// the page is done, whatever the browser does now is not our problem
	http_on_close(s->c, NULL, NULL);
	http_write(s->c, b.p, b.len);
	http_end_trailers(s->c, server_timing(s));
	buf_free(&b);
	search_free(s);
}

/* called whenever a job ends, the last one out turns the lights off */
static void
maybe_finish(struct search *s)
{
	if (!s->pending && !s->starting)
		search_finish(s);
}

static void
on_deadline(void *arg)
{
	search_finish(arg);
}

static void
on_client_close(void *arg)
{
	search_free(arg);
}

/* warm favicons and the images likely to be on screen before the page asks for them */
static void
prefetch(struct job *j)
{
	struct main_result *m;
	struct result *r;
	struct url u;
	int images = 0, thumbnails = 0;

	for (r = j->head; r; r = r->next) {
		if (r->type == RESULT_INFOBOX)
			image_prefetch(((struct infobox *)r)->img_src);
		if (r->type != RESULT_MAIN)
			continue;
		m = (struct main_result *)r;
		url_parse(j->a, m->url, &u);
		favicon_prefetch(j->filters->prefs.favicon,
		    starts_with(u.netloc, "xn--") ? idna_decode(j->a, u.netloc) : u.netloc);
		if (m->template && !strcmp(m->template, "images")) {
			if (images++ < PREFETCH_IMAGES)
				image_prefetch(*m->thumbnail_src ? m->thumbnail_src : m->img_src);
		} else if (m->thumbnail && *m->thumbnail && thumbnails++ < PREFETCH_THUMBNAILS) {
			image_prefetch(m->thumbnail);
		}
	}
}

void
job_done(struct job *j)
{
	struct engine *e = j->e;
	struct result *r;
	int found = 0;

	if (j->done)
		return;
	j->done = 1;
	for (r = j->head; r && !found; r = r->next)
		found = 1;
	stats_record(e, found ? OUTCOME_OK : OUTCOME_EMPTY, (now_ms() - j->start) / 1e3, NULL);
	prefetch(j);
	results_extend(&j->s->res, e, j->head);
	results_timing(&j->s->res, e, (now_ms() - j->start) / 1e3, j->load);
	e->continuous_errors = 0;
	e->suspend_end = 0;
	e->suspend_reason[0] = 0;
	job_cleanup(j);
	j->s->pending--;
	maybe_finish(j->s);
}

void
job_fail(struct job *j, const char *error, int suspend)
{
	struct engine *e = j->e;
	int secs;

	if (j->done)
		return;
	j->done = 1;
	results_unresponsive(&j->s->res, e, astrdup(&j->s->a, error), 0);
	results_timing(&j->s->res, e, (now_ms() - j->start) / 1e3, j->load);
	stats_record(e, OUTCOME_ERROR, (now_ms() - j->start) / 1e3,
	    j->detail ? aprintf(&j->s->a, "%s: %s", error, j->detail) : error);
	if (suspend != SUSPEND_NONE) {
		secs = suspend == SUSPEND_DEFAULT ? MIN(max_ban_time_on_fail, ban_time_on_fail) : suspend;
		e->continuous_errors++;
		e->suspend_end = now_ms() + secs * 1e3;
		snprintf(e->suspend_reason, sizeof(e->suspend_reason), "%s", error);
	}
	job_cleanup(j);
	j->s->pending--;
	maybe_finish(j->s);
}

static void
trampoline(struct fetch *f, void *arg)
{
	struct job *j = arg;
	job_cb cb = j->cb;

	curl_off_t us = 0;

	j->f = NULL;
	metrics_fetch(j->e, f);
	if (curl_easy_getinfo(f->h, CURLINFO_TOTAL_TIME_T, &us) == CURLE_OK)
		j->load += us / 1e6;
	cb(j, f);
	fetch_free(f);
}

void
job_send(struct job *j, struct fetch *f, job_cb cb)
{
	double left = j->s->deadline.when - now_ms();

	if (j->done) {
		fetch_free(f);
		return;
	}
	j->f = f;
	j->cb = cb;
	fetch_timeout(f, (long)MAX(left, 1));
	fetch_start(f, trampoline, j);
}

struct fetch *
job_request(struct job *j, const char *url)
{
	struct fetch *f = fetch_new(url);

	if (!j->e->no_accept_language)
		fetch_header(f, "Accept-Language: %s", j->filters->locale->accept_language);
	return f;
}

struct fetch *
job_get(struct job *j, const char *url)
{
	struct fetch *f = fetch_new(url);

	(void)j;
	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	return f;
}

int
job_ok(struct job *j, struct fetch *f)
{
	const char *msg;

	if (f->code == CURLE_OK)
		return 1;
	switch (f->code) {
	case CURLE_OPERATION_TIMEDOUT: msg = "timeout"; break;
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_PEER_FAILED_VERIFICATION: msg = "SSL error: certificate validation has failed"; break;
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT: msg = "HTTP connection error"; break;
	default: msg = "network error"; break;
	}
	j->detail = aprintf(j->a, "%s (%s)", f->error[0] ? f->error : curl_easy_strerror(f->code), fetch_host(f));
	job_fail(j, msg, SUSPEND_DEFAULT);
	return 0;
}

int
job_http_ok(struct job *j, struct fetch *f)
{
	const char *body = fetch_body(f);
	long st = f->status;

	if (!job_ok(j, f))
		return 0;
	if (st < 400)
		return 1;
	j->detail = aprintf(j->a, "HTTP %ld from %s", st, fetch_host(f));
	if (starts_with(f->server, "cloudflare")) {
		if (((st == 429 || st == 503) &&
		        (strstr(body, "__cf_chl_jschl_tk__=") ||
		            (strstr(body, "/cdn-cgi/challenge-platform/") && strstr(body, "orchestrate/jsch/v1") &&
		                strstr(body, "window._cf_chl_enter(")))) ||
		    (st == 403 && strstr(body, "__cf_chl_captcha_tk__="))) {
			job_fail(j, "CAPTCHA", suspend_cf_captcha);
			return 0;
		}
		if (st == 403 && strstr(body, "<span class=\"cf-error-code\">1020</span>")) {
			job_fail(j, "access denied", suspend_cf_access_denied);
			return 0;
		}
	}
	if (st == 503 && strstr(body, "\"https://www.google.com/recaptcha/")) {
		job_fail(j, "CAPTCHA", suspend_recaptcha);
		return 0;
	}
	if (st == 402 || st == 403) {
		job_fail(j, "access denied", suspend_access_denied);
		return 0;
	}
	if (st == 429) {
		job_fail(j, "too many requests", suspend_too_many_requests);
		return 0;
	}
	job_fail(j, "HTTP error", SUSPEND_DEFAULT);
	return 0;
}

void
search_start(struct conn *c, const char *raw, int pageno, const char *tokens, const struct filters *f)
{
	struct search *s = xcalloc(1, sizeof(*s));
	struct buf b = {0};
	struct engine *e;
	struct job *j;
	size_t i;

	//
	// Copy what we need
	//
	// the request's arena is freed as soon as we return, but the search
	// lives until the engines are done, so everything gets its own copy
	//
	s->c = c;
	s->res.a = &s->a;
	s->raw = astrdup(&s->a, raw);
	s->filters = *f;
	s->query = query_parse(&s->a, raw, &s->filters); // also takes the !bangs and :language out
	s->pageno = pageno;
	s->tokens = tokens ? astrdup(&s->a, tokens) : NULL;
	s->filters.language = astrdup(&s->a, s->filters.language);
	s->filters.client_ip = astrdup(&s->a, f->client_ip);
	s->filters.client_agent = astrdup(&s->a, f->client_agent);
	s->filters.client_language = astrdup(&s->a, f->client_language);
	if (s->filters.prefs.language)
		s->filters.prefs.language = astrdup(&s->a, s->filters.prefs.language);
	if (f->time_range)
		s->filters.time_range = astrdup(&s->a, f->time_range);
	s->start = now_ms();
	s->jobs = xcalloc(nengines, sizeof(*s->jobs));

	//
	// Send the head now
	//
	// the engines take a second or more, no reason to make the browser wait
	// for the page's css and scripts as well. the rest follows when they answer
	//
	render_head(&b, s);
	http_begin(c, 200, STREAM_HEADERS);
	http_write(c, b.p, b.len);
	buf_free(&b);

	// whoever is slowest loses, the deadline finishes the page without them
	ev_timer_start(&s->deadline, filters_timeout(&s->filters) * 1e3, on_deadline, s);

	// if the browser goes away we stop everything and free the search
	http_on_close(c, on_client_close, s);

	//
	// Start the engines
	//
	// starting stops an engine that answers instantly from finishing the
	// page before the others have even been started
	//
	s->starting = 1;
	for (i = 0; i < nengines; i++) {
		e = engines[i];

		// not every engine has page 2
		if (pageno > 1 && !e->paging)
			continue;
		if (e->max_page && pageno > e->max_page)
			continue;

		// only bangs and filters, nothing to search for
		if (!*s->query)
			break;

		// a bang picks the engines by hand, otherwise it is the category and
		// whatever the user switched off in preferences
		if (s->filters.engines ? !(s->filters.engines & ENGINE_BIT(e)) :
		                         !engine_in(e, s->filters.category) ||
		            !prefs_engine_on(&s->filters.prefs, s->filters.category, e))
			continue;
		if (s->filters.time_range && !e->time_range)
			continue;

		// it failed recently, leave it alone until the ban is over
		if (e->suspend_end >= now_ms()) {
			results_unresponsive(&s->res, e, e->suspend_reason, 1);
			continue;
		}
		// all good, give it a job
		j = &s->jobs[s->njobs++];
		j->s = s;
		j->e = e;
		j->a = &s->a;
		j->query = s->query;
		j->pageno = pageno;
		j->token = s->tokens ? query_param(&s->a, s->tokens, e->name) : NULL;
		j->filters = &s->filters;
		j->tail = &j->head;
		j->start = now_ms();
		s->pending++;
		e->search(j); // may answer right away, even finish the whole search
	}
	s->starting = 0;

	// every engine answered (or there were none) while we were still starting
	maybe_finish(s);
}
