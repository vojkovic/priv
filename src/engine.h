#ifndef ENGINE_H
#define ENGINE_H

#include <stdio.h>

#include "../lib/yyjson/yyjson.h"
#include "fetch.h"
#include "result.h"
#include "search.h"
#include "util.h"
#include "xml.h"

#define SUSPEND_NONE (-1)
#define SUSPEND_DEFAULT (-2)

struct job;
struct locale;

struct engine {
	const char *name;
	const char *shortcut;
	const char *website;
	const char *bang;
	const char *category;
	int paging, max_page, time_range, safesearch, starts_off;
	int no_accept_language;
	void (*init)(void);
	void (*search)(struct job *j);

	int index, enabled;
	int continuous_errors;
	double suspend_end;
	char suspend_reason[128];
};

typedef void (*job_cb)(struct job *j, struct fetch *f);

struct job {
	struct search *s;
	struct engine *e;
	struct arena *a;
	const char *query;
	int pageno;
	const struct filters *filters;
	struct fetch *f;
	job_cb cb;
	struct result *head, **tail;
	double start, load;
	int done;
	const char *detail;
	const char *token, *next_token;
	struct cleanup {
		void (*fn)(void *);
		void *p;
	} *cleanup;
	int ncleanup, capcleanup;
};

extern struct engine *engines[];
extern size_t nengines;

void engines_init(void);
int engine_in(const struct engine *e, const char *category);
struct engine *engine_find(const char *name);

struct fetch *job_request(struct job *j, const char *url);
struct fetch *job_get(struct job *j, const char *url);
void job_send(struct job *j, struct fetch *f, job_cb cb);
void job_on_cleanup(struct job *j, void (*fn)(void *), void *p);
void job_done(struct job *j);
void job_fail(struct job *j, const char *error, int suspend);
int job_ok(struct job *j, struct fetch *f);
int job_http_ok(struct job *j, struct fetch *f);

struct main_result *add_main(struct job *j, const char *url, const char *title, const char *content);
struct main_result *add_image(struct job *j, const char *url, const char *title, const char *img_src);
struct main_result *add_video(struct job *j, const char *url, const char *title, const char *content);
struct main_result *add_paper(struct job *j, const char *url, const char *title, const char *content);
void result_attr(struct job *j, struct main_result *m, const char *label, const char *value);
struct infobox *add_infobox(struct job *j, const char *title);
void infobox_url(struct job *j, struct infobox *ib, const char *title, const char *url, const char *entity,
    int official);
void infobox_also(struct job *j, struct infobox *ib, const char *title, const char *description);
void infobox_attr(struct job *j, struct infobox *ib, const char *label, const char *value, const char *entity);
void add_answer(struct job *j, const char *answer, const char *url);

const char *time_range_code(const struct filters *f, const char *day, const char *week, const char *month,
    const char *year);

htmlDocPtr job_html(struct job *j, struct fetch *f);
xmlDocPtr job_xml(struct job *j, struct fetch *f);
yyjson_doc *job_json(struct job *j, const char *s, size_t len);
const char *json_str(yyjson_val *obj, const char *key);
const char *json_text(struct arena *a, yyjson_val *obj, const char *key);
const char *format_duration(struct arena *a, long seconds);
const char *format_date(struct arena *a, const char *iso);
const char *format_count(struct arena *a, long long n);

#endif
