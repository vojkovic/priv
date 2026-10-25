#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engine.h"
#include "favicon.h"
#include "icons.h"
#include "proxy.h"
#include "render.h"
#include "search.h"
#include "stats.h"

#define PREFETCH_THUMBNAILS 10
#define PREFETCH_IMAGES 6

#define STREAM_HEADERS \
	"Content-Type: text/html; charset=utf-8\r\n" \
	"Cache-Control: no-store\r\n" \
	"X-Accel-Buffering: no\r\n" \
	"Trailer: Server-Timing\r\n" SECURITY_HEADERS

const struct category categories[] = {
    {"general", "General", ICON_SEARCH},
    {"images", "Images", ICON_IMAGES},
    {"videos", "Videos", ICON_VIDEOS},
    {"news", "News", ICON_NEWS},
    {"music", "Music", ICON_MUSIC},
    {"tech", "Tech", ICON_TECH},
    {"research", "Research", ICON_RESEARCH},
};

const size_t ncategories = LEN(categories);

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
server_timing(struct search *s, double render_ms)
{
	struct results *r = &s->res;
	struct buf b = {0};
	const char *out;
	size_t i;

	if (r->ntimings)
		qsort(r->timings, r->ntimings, sizeof(*r->timings), by_total);
	buf_printf(&b, "Server-Timing: total;dur=%.3f, render;dur=%.3f", now_ms() - s->start, render_ms);
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
	double render_start;
	size_t i;

	ev_timer_stop(&s->deadline);
	for (i = 0; i < s->njobs; i++) {
		if (s->jobs[i].done)
			continue;
		s->jobs[i].done = 1;
		job_cleanup(&s->jobs[i]);
		results_unresponsive(&s->res, s->jobs[i].e, "timeout", 0);
		results_timing(&s->res, s->jobs[i].e, (now_ms() - s->start) / 1e3, s->jobs[i].load);
		stats_record(s->jobs[i].e, OUTCOME_TIMEOUT, (now_ms() - s->start) / 1e3, "timeout");
	}
	s->elapsed = (now_ms() - s->start) / 1e3;
	results_close(&s->res);
	render_start = now_ms();
	render_results(&b, s);
	http_on_close(s->c, NULL, NULL);
	http_write(s->c, b.p, b.len);
	http_end_trailers(s->c, server_timing(s, now_ms() - render_start));
	buf_free(&b);
	search_free(s);
}

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
		favicon_prefetch(starts_with(u.netloc, "xn--") ? idna_decode(j->a, u.netloc) : u.netloc);
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

static void
add(struct job *j, struct result *r, enum result_type type)
{
	r->type = type;
	r->engine = j->e;
	r->next = NULL;
	*j->tail = r;
	j->tail = &r->next;
}

struct main_result *
add_main(struct job *j, const char *url, const char *title, const char *content)
{
	struct main_result *m = amalloc(j->a, sizeof(*m));

	memset(m, 0, sizeof(*m));
	m->url = astrdup(j->a, url ? url : "");
	m->title = astrdup(j->a, title ? title : "");
	m->content = astrdup(j->a, content ? content : "");
	add(j, &m->base, RESULT_MAIN);
	return m;
}

struct main_result *
add_image(struct job *j, const char *url, const char *title, const char *img_src)
{
	struct main_result *m = add_main(j, url, title, "");

	m->template = "images";
	m->img_src = astrdup(j->a, img_src ? img_src : "");
	m->thumbnail_src = m->resolution = m->source = m->img_format = "";
	return m;
}

struct main_result *
add_video(struct job *j, const char *url, const char *title, const char *content)
{
	struct main_result *m = add_main(j, url, title, content);

	m->template = "videos";
	return m;
}

struct main_result *
add_paper(struct job *j, const char *url, const char *title, const char *content)
{
	struct main_result *m = add_main(j, url, title, content);

	m->template = "paper";
	return m;
}

void
result_attr(struct job *j, struct main_result *m, const char *label, const char *value)
{
	struct result_attr *at, **tail;

	if (!value || !*value)
		return;
	at = amalloc(j->a, sizeof(*at));
	at->label = label;
	at->value = astrdup(j->a, value);
	at->next = NULL;
	for (tail = &m->attrs; *tail; tail = &(*tail)->next)
		;
	*tail = at;
}

struct infobox *
add_infobox(struct job *j, const char *title)
{
	struct infobox *ib = amalloc(j->a, sizeof(*ib));

	memset(ib, 0, sizeof(*ib));
	ib->title = astrdup(j->a, title);
	add(j, &ib->base, RESULT_INFOBOX);
	return ib;
}

void
infobox_url(struct job *j, struct infobox *ib, const char *title, const char *url, const char *entity, int official)
{
	struct infobox_url *u, **tail;

	if (!web_url(url))
		return;
	u = amalloc(j->a, sizeof(*u));
	u->title = astrdup(j->a, title ? title : "");
	u->url = astrdup(j->a, url);
	u->entity = entity ? astrdup(j->a, entity) : NULL;
	u->official = official;
	u->next = NULL;
	for (tail = &ib->urls; *tail; tail = &(*tail)->next)
		;
	*tail = u;
}

void
infobox_also(struct job *j, struct infobox *ib, const char *title, const char *description)
{
	struct infobox_url *u = amalloc(j->a, sizeof(*u)), **tail;

	memset(u, 0, sizeof(*u));
	u->title = astrdup(j->a, title);
	u->entity = description && *description ? astrdup(j->a, description) : NULL;
	for (tail = &ib->also; *tail; tail = &(*tail)->next)
		;
	*tail = u;
}

void
infobox_attr(struct job *j, struct infobox *ib, const char *label, const char *value, const char *entity)
{
	struct infobox_attr *a = amalloc(j->a, sizeof(*a)), **tail;

	a->label = label ? astrdup(j->a, label) : NULL;
	a->value = astrdup(j->a, value ? value : "");
	a->entity = entity ? astrdup(j->a, entity) : NULL;
	a->next = NULL;
	for (tail = &ib->attrs; *tail; tail = &(*tail)->next)
		;
	*tail = a;
}

void
add_answer(struct job *j, const char *answer, const char *url)
{
	struct answer *a = amalloc(j->a, sizeof(*a));

	memset(a, 0, sizeof(*a));
	a->answer = astrdup(j->a, answer);
	a->url = web_url(url) ? astrdup(j->a, url) : NULL;
	add(j, &a->base, RESULT_ANSWER);
}

void
job_on_cleanup(struct job *j, void (*fn)(void *), void *p)
{
	struct cleanup *grown;

	/* grows in the search's arena, so an engine can parse as many documents as it needs */
	if (j->ncleanup == j->capcleanup) {
		j->capcleanup = j->capcleanup ? j->capcleanup * 2 : 8;
		grown = amalloc(j->a, (size_t)j->capcleanup * sizeof(*grown));
		if (j->ncleanup)
			memcpy(grown, j->cleanup, (size_t)j->ncleanup * sizeof(*grown));
		j->cleanup = grown;
	}
	j->cleanup[j->ncleanup].fn = fn;
	j->cleanup[j->ncleanup++].p = p;
}

static void
free_html(void *p)
{
	xmlFreeDoc(p);
}

static void
free_json(void *p)
{
	yyjson_doc_free(p);
}

htmlDocPtr
job_html(struct job *j, struct fetch *f)
{
	const char *s = fetch_body(f);
	htmlDocPtr d;

	if (starts_with(s, "<?xml") && strstr(s, "?>"))
		s = strstr(s, "?>") + 2;
	d = html_parse(s, strlen(s));
	if (d)
		job_on_cleanup(j, free_html, d);
	return d;
}

xmlDocPtr
job_xml(struct job *j, struct fetch *f)
{
	xmlDocPtr d = xml_parse(f->body.p ? f->body.p : "", f->body.len);

	if (d)
		job_on_cleanup(j, free_html, d);
	return d;
}

yyjson_doc *
job_json(struct job *j, const char *s, size_t len)
{
	yyjson_doc *d = yyjson_read(s, len, YYJSON_READ_ALLOW_INVALID_UNICODE);

	if (d)
		job_on_cleanup(j, free_json, d);
	return d;
}

const char *
json_text(struct arena *a, yyjson_val *obj, const char *key)
{
	yyjson_val *v = yyjson_obj_get(obj, key);

	if (yyjson_is_str(v))
		return yyjson_get_str(v);
	if (yyjson_is_int(v))
		return aprintf(a, "%lld", (long long)yyjson_get_sint(v));
	if (yyjson_is_real(v))
		return aprintf(a, "%g", yyjson_get_real(v));
	return "";
}

const char *
json_str(yyjson_val *obj, const char *key)
{
	const char *s = yyjson_get_str(yyjson_obj_get(obj, key));

	return s ? s : "";
}

const char *
format_duration(struct arena *a, long secs)
{
	if (secs <= 0)
		return "";
	if (secs >= 3600)
		return aprintf(a, "%ld:%02ld:%02ld", secs / 3600, secs / 60 % 60, secs % 60);
	return aprintf(a, "%ld:%02ld", secs / 60, secs % 60);
}

const char *
format_date(struct arena *a, const char *iso)
{
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov",
	    "Dec"};
	int y, mo, d;

	if (!iso || sscanf(iso, "%4d-%2d-%2d", &y, &mo, &d) != 3 || mo < 1 || mo > 12) // NOLINT(cert-err34-c)
		return iso ? astrdup(a, iso) : "";
	return aprintf(a, "%s %d, %d", months[mo - 1], d, y);
}

const char *
format_count(struct arena *a, long long n)
{
	if (n >= 1000000000)
		return aprintf(a, "%.1fB", n / 1e9);
	if (n >= 1000000)
		return aprintf(a, "%.1fM", n / 1e6);
	if (n >= 1000)
		return aprintf(a, "%.1fK", n / 1e3);
	return aprintf(a, "%lld", n);
}

const char *
time_range_code(const struct filters *f, const char *day, const char *week, const char *month, const char *year)
{
	const char *ranges[] = {"day", "week", "month", "year"};
	const char *codes[] = {day, week, month, year};
	size_t i;

	for (i = 0; f->time_range && i < LEN(ranges); i++)
		if (!strcmp(f->time_range, ranges[i]))
			return codes[i];
	return NULL;
}

const struct locale *
client_locale(struct arena *a, const char *header)
{
	char *list = astrdup(a, header), *item, *save, *semi, *q, *best = NULL;
	const struct locale *l;
	double bq = -1, qv;
	size_t i;

	for (item = strtok_r(list, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
		qv = 1;
		if ((semi = strchr(item, ';'))) {
			*semi = 0;
			if ((q = strstr(semi + 1, "q=")))
				qv = strtod(q + 2, NULL);
		}
		item = str_strip(item);
		if (!IS_ALPHA(item[0]) || qv <= bq)
			continue;
		bq = qv;
		best = item;
	}
	if (!best)
		return NULL;
	/* auto keeps only the language, en-GB searches as en */
	best[strcspn(best, "-_")] = 0;
	for (i = 0; best[i]; i++)
		best[i] = (char)tolower((unsigned char)best[i]);
	return (l = locale_find(best)) != &locales[0] ? l : NULL;
}

static const char *
category_find(const char *name)
{
	size_t i;

	for (i = 0; name && i < ncategories; i++)
		if (!strcmp(categories[i].name, name))
			return categories[i].name;
	return NULL;
}

static char *
lower(char *s)
{
	char *p;

	for (p = s; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return s;
}

static int
bang(struct arena *a, const char *value, struct filters *f)
{
	char *v = lower(astrdup(a, value)), *p;
	const char *cat;
	struct engine *e;
	size_t i;

	for (p = v; *p; p++)
		if (*p == '_')
			*p = ' ';
	for (i = 0; i < nengines; i++) {
		e = engines[i];
		if ((e->shortcut && !strcmp(e->shortcut, v)) || !strcmp(e->name, v)) {
			if (!f->engines)
				f->category = e->categories[0];
			/* the first bang picks the category, later ones only add engines that belong to it */
			if (engine_in(e, f->category))
				f->engines |= ENGINE_BIT(e);
			return 1;
		}
	}
	if (!(cat = category_find(v)))
		return 0;
	if (!f->engines)
		f->category = cat;
	for (i = 0; i < nengines; i++)
		if (engine_in(engines[i], cat) && engine_in(engines[i], f->category))
			f->engines |= ENGINE_BIT(engines[i]);
	return 1;
}

static int
language(struct arena *a, const char *value, struct filters *f)
{
	char *v = lower(astrdup(a, value)), *p, *name, *country, *english;
	const struct locale *found = NULL;
	size_t i;

	for (p = v; *p; p++)
		if (*p == '_')
			*p = '-';
	for (i = 1; i < nlocales; i++) {
		if (!strcasecmp(locales[i].tag, v)) {
			found = &locales[i];
			break;
		}
		name = lower(astrdup(a, locales[i].names));
		country = strchr(name, '|');
		*country++ = 0;
		english = strchr(country, '|');
		*english++ = 0;
		for (p = country; *p; p++)
			if (*p == ' ')
				*p = '-';
		if ((!strcmp(name, v) || !strcmp(english, v) || !strcmp(country, v)) &&
		    (!found || strchr(found->tag, '-')))
			found = &locales[i];
	}
	if (!found && strcmp(v, "auto"))
		return 0;
	f->language = found ? found->tag : "auto";
	if (found)
		f->locale = found;
	return 1;
}

static int
timeout(const char *value, struct filters *f)
{
	const char *p;
	long n;

	for (p = value; *p; p++)
		if (!IS_DIGIT(*p))
			return 0;
	if (!*value)
		return 0;
	n = parse_long(value, 0);
	f->timeout = MIN(n < 100 ? (double)n : n / 1000.0, 10.0);
	return 1;
}

const char *
query_parse(struct arena *a, const char *raw, struct filters *f)
{
	char *q = astrdup(a, raw), *w, *save;
	struct buf b = {0};
	const char *out;
	int special;

	for (w = strtok_r(q, " \t\n\r", &save); w; w = strtok_r(NULL, " \t\n\r", &save)) {
		special = 0;
		if (w[0] == '<')
			special = timeout(w + 1, f);
		else if (w[0] == ':' && w[1])
			special = language(a, w + 1, f);
		else if (w[0] == '!' && w[1] && w[1] != '!')
			special = bang(a, w + 1, f);
		if (special)
			continue;
		if (b.len)
			buf_putc(&b, ' ');
		buf_puts(&b, w);
	}
	out = astrdup(a, buf_str(&b));
	buf_free(&b);
	return out;
}

void
filters_parse(struct arena *a, struct request *r, struct filters *f)
{
	const char *p, *t;

	prefs_parse(a, r, &f->prefs);
	f->category = category_find(query_param(a, r->query, "categories"));
	if (!f->category)
		f->category = "general";
	f->engines = 0;
	f->timeout = 0;
	p = query_param(a, r->query, "language");
	f->language = p && *p ? p : f->prefs.language ? f->prefs.language : "auto";
	f->browser = client_locale(a, r->accept_language);
	if (!f->browser)
		f->browser = &locales[0];
	if (!strcmp(f->language, "auto")) {
		f->locale = f->browser;
	} else {
		f->locale = locale_find(f->language);
		f->language = f->locale->tag;
	}
	t = query_param(a, r->query, "time_range");
	f->time_range =
	    t && (!strcmp(t, "day") || !strcmp(t, "week") || !strcmp(t, "month") || !strcmp(t, "year")) ? t : NULL;
	p = query_param(a, r->query, "safesearch");
	f->safesearch = p && *p >= '0' && *p <= '2' && !p[1] ? *p - '0' : f->prefs.safesearch;
}

void
search_start(struct conn *c, const char *raw, int pageno, const struct filters *f)
{
	struct search *s = xcalloc(1, sizeof(*s));
	struct buf b = {0};
	struct engine *e;
	struct job *j;
	size_t i;

	s->c = c;
	s->res.a = &s->a;
	s->raw = astrdup(&s->a, raw);
	s->filters = *f;
	s->query = query_parse(&s->a, raw, &s->filters);
	s->pageno = pageno;
	s->filters.language = astrdup(&s->a, s->filters.language);
	if (s->filters.prefs.language)
		s->filters.prefs.language = astrdup(&s->a, s->filters.prefs.language);
	if (f->time_range)
		s->filters.time_range = astrdup(&s->a, f->time_range);
	s->start = now_ms();
	s->jobs = xcalloc(nengines, sizeof(*s->jobs));

	render_head(&b, s);
	http_begin(c, 200, aprintf(&s->a, STREAM_HEADERS "Server-Timing: head;dur=%.3f\r\n", now_ms() - s->start));
	http_write(c, b.p, b.len);
	buf_free(&b);

	ev_timer_start(&s->deadline, (s->filters.timeout > 0 ? s->filters.timeout : search_timeout) * 1e3, on_deadline,
	    s);
	http_on_close(c, on_client_close, s);
	s->starting = 1;
	for (i = 0; i < nengines; i++) {
		e = engines[i];
		if (pageno > 1 && !e->paging)
			continue;
		if (e->max_page && pageno > e->max_page)
			continue;
		if (!*s->query)
			break;
		if (s->filters.engines ? !(s->filters.engines & ENGINE_BIT(e)) :
		                         !engine_in(e, s->filters.category) ||
		            !prefs_engine_on(&s->filters.prefs, s->filters.category, e))
			continue;
		if (s->filters.time_range && !e->time_range)
			continue;
		if (e->suspend_end >= now_ms()) {
			results_unresponsive(&s->res, e, e->suspend_reason, 1);
			continue;
		}
		j = &s->jobs[s->njobs++];
		j->s = s;
		j->e = e;
		j->a = &s->a;
		j->query = s->query;
		j->pageno = pageno;
		j->filters = &s->filters;
		j->tail = &j->head;
		j->start = now_ms();
		s->pending++;
		e->search(j);
	}
	s->starting = 0;
	maybe_finish(s);
}
