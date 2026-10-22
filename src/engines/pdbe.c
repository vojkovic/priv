#include <string.h>

#include "../engine.h"

static int
unpublished(const char *status)
{
	static const char *codes[] = {"HPUB", "HOLD", "PROC", "WAIT", "AUTH", "AUCO", "REPL", "POLC", "REFI", "TRSF",
	    "WDRN"};
	size_t i;

	for (i = 0; i < LEN(codes); i++)
		if (!strcmp(codes[i], status))
			return 1;
	return 0;
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_val *docs, *r, *authors;
	const char *id, *title, *content;
	struct main_result *m;
	yyjson_doc *doc;
	size_t i, n;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	docs = yyjson_ptr_get(yyjson_doc_get_root(doc), "/response/docs");
	yyjson_arr_foreach (docs, i, n, r) {
		if (unpublished(json_str(r, "status")))
			continue;
		id = json_text(j->a, r, "pdb_id");
		title = json_text(j->a, r, "title");
		if (!strcmp(json_str(r, "status"), "OBS")) {
			if (!*json_str(r, "superseded_by"))
				continue;
			add_main(j, aprintf(j->a, "https://www.ebi.ac.uk/pdbe/entry/pdb/%s", id),
			    aprintf(j->a, "%s (OBSOLETE)", title),
			    aprintf(j->a,
			        "This entry has been superseded by: https://www.ebi.ac.uk/pdbe/entry/pdb/%s (%s)",
			        json_str(r, "superseded_by"), json_str(r, "superseded_by")));
			continue;
		}
		authors = yyjson_obj_get(r, "entry_author_list");
		if (*json_str(r, "journal"))
			content = aprintf(j->a, "%s - %s %s (%s) %s (%s)", json_text(j->a, r, "citation_title"),
			    yyjson_get_str(yyjson_arr_get_first(authors)) ?
			        yyjson_get_str(yyjson_arr_get_first(authors)) :
			        "",
			    json_str(r, "journal"), json_text(j->a, r, "journal_volume"),
			    json_text(j->a, r, "journal_page"), json_text(j->a, r, "citation_year"));
		else
			content = aprintf(j->a, "%s - %s  ()  (%s)", json_text(j->a, r, "citation_title"),
			    yyjson_get_str(yyjson_arr_get_first(authors)) ?
			        yyjson_get_str(yyjson_arr_get_first(authors)) :
			        "",
			    json_text(j->a, r, "release_year"));
		m = add_main(j, aprintf(j->a, "https://www.ebi.ac.uk/pdbe/entry/pdb/%s", id), title, content);
		m->thumbnail = aprintf(j->a,
		    "https://www.ebi.ac.uk/pdbe/static/entry/%s_deposited_chain_front_image-200x200.png", id);
	}
	job_done(j);
}

static void
search(struct job *j)
{
	struct fetch *f = job_request(j, "https://www.ebi.ac.uk/pdbe/search/pdb/select?");
	struct buf body = {0};

	form_add(&body, "q", j->query);
	form_add(&body, "wt", "json");
	fetch_post(f, buf_str(&body), "application/x-www-form-urlencoded");
	buf_free(&body);
	job_send(j, f, response);
}

struct engine engine_pdbe = {
    .name = "pdbe",
    .shortcut = "pdb",
    .website = "www.ebi.ac.uk",
    .categories = {"research"},
    .search = search,
};
