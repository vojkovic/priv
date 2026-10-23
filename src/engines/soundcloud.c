#include <stdlib.h>
#include <string.h>

#include "../engine.h"
#include "../token.h"

#define PAGE_SIZE 10
#define MAX_SCRIPTS 32

static struct {
	char *urls[MAX_SCRIPTS];
	int n;
} scripts;

static void script_response(struct fetch *f, void *arg);

static void
next_script(struct token *t)
{
	struct fetch *f;
	char *url;

	if (!scripts.n) {
		token_fail(t, "server API error", SUSPEND_NONE);
		return;
	}
	url = scripts.urls[--scripts.n];
	f = fetch_new(url);
	free(url);
	fetch_timeout(f, 10000);
	fetch_start(f, script_response, t);
}

static void
script_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	const char *id;
	size_t n = 0;

	if (f->code == CURLE_OK && f->status < 400 && (id = find_between(fetch_body(f), "client_id:\"", "\"", &n)) &&
	    n) {
		char *v = xmalloc(n + 1);
		memcpy(v, id, n);
		v[n] = 0;
		while (scripts.n)
			free(scripts.urls[--scripts.n]);
		token_set(t, v);
		free(v);
	} else {
		next_script(t);
	}
	fetch_free(f);
}

static void
home_response(struct fetch *f, void *arg)
{
	struct token *t = arg;
	struct arena a = {0};
	struct xnodes tags;
	htmlDocPtr doc;
	int i;

	if (f->code != CURLE_OK || f->status >= 400 || !(doc = html_parse(f->body.p ? f->body.p : "", f->body.len))) {
		token_fail(t, f->code != CURLE_OK ? "network error" : "server API error",
		    f->code != CURLE_OK ? SUSPEND_DEFAULT : SUSPEND_NONE);
		fetch_free(f);
		return;
	}
	tags = xpath((xmlNodePtr)doc, "//script[contains(@src, '/assets/')]/@src");
	for (i = 0; i < tags.n && scripts.n < MAX_SCRIPTS; i++)
		scripts.urls[scripts.n++] = xstrdup(node_text(&a, tags.v[i]));
	xnodes_free(&tags);
	xmlFreeDoc(doc);
	arena_free(&a);
	fetch_free(f);
	next_script(t);
}

static void
token_fetch(struct token *t)
{
	struct fetch *f = fetch_new("https://soundcloud.com");

	while (scripts.n)
		free(scripts.urls[--scripts.n]);
	curl_easy_setopt(f->h, CURLOPT_FOLLOWLOCATION, 1L);
	fetch_timeout(f, 10000);
	fetch_start(f, home_response, t);
}

static struct token token = {
    .name = "soundcloud",
    .ttl = 86400,
    .refresh = 80000,
    .fetch = token_fetch,
};

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *collection, *r, *user, *v;
	struct main_result *m;
	const char *kind, *url, *thumb, *desc, *label;
	yyjson_doc *doc;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	collection = yyjson_obj_get(yyjson_doc_get_root(doc), "collection");
	yyjson_arr_foreach (collection, i, n, r) {
		kind = json_str(r, "kind");
		if ((strcmp(kind, "track") && strcmp(kind, "playlist")) || !*(url = json_str(r, "permalink_url")))
			continue;
		desc = json_str(r, "description");
		label = json_str(r, "label_name");
		m = add_main(j, url, json_str(r, "title"),
		    *desc && *label ? aprintf(j->a, "%s / %s", desc, label) :
		        *desc       ? desc :
		                      label);
		m->pubdate = (char *)format_date(j->a, json_str(r, "last_modified"));
		user = yyjson_obj_get(r, "user");
		thumb = json_str(r, "artwork_url");
		m->thumbnail = astrdup(j->a, *thumb ? thumb : json_str(user, "avatar_url"));
		m->length = format_duration(j->a, (long)(yyjson_get_num(yyjson_obj_get(r, "duration")) / 1000));
		if ((v = yyjson_obj_get(r, "playback_count")) && yyjson_get_num(v) > 0)
			m->views = format_count(j->a, (long long)yyjson_get_num(v));
		m->author = astrdup(j->a, json_str(user, "full_name"));
	}
	job_done(j);
}

static const char *
app_locale(const char *tag)
{
	static const char *map[][2] = {{"de", "de"}, {"en", "en"}, {"es", "es"}, {"fr", "fr"}, {"oc", "fr"},
	    {"it", "it"}, {"nl", "nl"}, {"pl", "pl"}, {"szl", "pl"}, {"pt", "pt_BR"}, {"pap", "pt_BR"}, {"sv", "sv"}};
	size_t i, n = strcspn(tag, "-");

	for (i = 0; i < LEN(map); i++)
		if (strlen(map[i][0]) == n && !strncmp(map[i][0], tag, n))
			return map[i][1];
	return "en";
}

static void
request(struct job *j, const char *client_id)
{
	struct buf q = {0};
	struct fetch *f;
	char offset[16];

	snprintf(offset, sizeof(offset), "%d", (j->pageno - 1) * PAGE_SIZE);
	form_add(&q, "q", j->query);
	form_add(&q, "offset", offset);
	form_add(&q, "limit", "10");
	form_add(&q, "facet", "model");
	form_add(&q, "client_id", client_id);
	form_add(&q, "app_locale", app_locale(j->filters->language));
	f = job_request(j, aprintf(j->a, "https://api-v2.soundcloud.com/search?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

static void
init(void)
{
	token_warm(&token);
}

static void
search(struct job *j)
{
	token_get(j, &token, request);
}

struct engine engine_soundcloud = {
    .name = "soundcloud",
    .shortcut = "sc",
    .website = "soundcloud.com",
    .bang = "https://soundcloud.com/search?q=%s",
    .categories = {"music"},
    .paging = 1,
    .init = init,
    .search = search,
};
