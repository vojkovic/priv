#ifndef RESULT_H
#define RESULT_H

#include <stdint.h>

#include "util.h"

struct engine;

#define ENGINE_BIT(e) ((uint64_t)1 << (e)->index)

enum result_type {
	RESULT_MAIN,
	RESULT_INFOBOX,
	RESULT_ANSWER,
};

struct result {
	enum result_type type;
	const struct engine *engine;
	struct result *next;
};

struct result_attr {
	const char *label, *value;
	struct result_attr *next;
};

struct main_result {
	struct result base;
	const char *template;
	char *url, *title, *content, *thumbnail, *img_src, *pubdate;
	char *thumbnail_src, *resolution, *source, *img_format;
	const char *length, *author, *views, *pdf_url;
	struct result_attr *attrs;
	struct url parsed;
	uint64_t engines;
	int *positions, npositions;
	double score;
	const char *category;
};

struct infobox_url {
	char *title, *url, *entity;
	int official;
	struct infobox_url *next;
};

struct infobox_attr {
	char *label, *value, *entity;
	struct infobox_attr *next;
};

struct infobox {
	struct result base;
	char *title, *id, *content, *img_src, *description;
	struct infobox_url *urls, *also;
	struct infobox_attr *attrs;
	uint64_t engines;
};

struct answer {
	struct result base;
	char *answer, *url;
};

struct unresponsive {
	const struct engine *engine;
	const char *error;
	int suspended;
};

struct timing {
	const struct engine *engine;
	double seconds;
};

struct results {
	struct arena *a;
	struct main_result **main;
	size_t nmain, capmain;
	struct infobox **infoboxes;
	size_t ninfoboxes, capinfoboxes;
	struct answer **answers;
	size_t nanswers, capanswers;
	struct unresponsive *unresponsive;
	size_t nunresponsive, capunresponsive;
	struct timing *timings;
	size_t ntimings, captimings;
	int paging;
};

void results_extend(struct results *r, const struct engine *e, struct result *list);
void results_unresponsive(struct results *r, const struct engine *e, const char *error, int suspended);
void results_timing(struct results *r, const struct engine *e, double seconds);
void results_close(struct results *r);

#endif
