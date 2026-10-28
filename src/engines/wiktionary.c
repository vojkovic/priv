#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "engines/engine.h"

#define MAX_WORD 64
#define MAX_CLASSES 4 // parts of speech shown, wiktionary can have a dozen
#define MAX_SENSES 8 // the card shows two and hides the rest behind "more"

//
// Wiktionary definitions, shown as an infobox
//
// this runs for every query, but only words wiktionary knows come back (the
// rest is a 404). the card only shows when no other engine has an infobox,
// that is decided when the page is rendered.
//

/* senses that only point at another word, which a card has no use for */
static int
form_of(const char *text)
{
	static const char *const starts[] = {"plural of", "alternative", "misspelling", "obsolete", "initialism",
	    "abbreviation", "present participle", "simple past", "past participle", "third-person", "inflection of",
	    "comparative", "superlative", "eye dialect", "nonstandard", "dated form", "archaic", "synonym of",
	    "clipping of", "contraction of", "genitive", "diminutive of", "pronunciation spelling"};
	size_t i;

	for (i = 0; i < LEN(starts); i++)
		if (!strncasecmp(text, starts[i], strlen(starts[i])))
			return 1;
	return 0;
}

/* the first example sentence of a sense, as plain text */
static const char *
example(struct job *j, yyjson_val *sense)
{
	yyjson_val *e = yyjson_arr_get(yyjson_obj_get(sense, "parsedExamples"), 0);

	return e ? html_to_text(j->a, json_str(e, "example")) : "";
}

static void
response(struct job *j, struct fetch *f)
{
	const char *base = j->filters->locale->tag;
	char lang[8];
	yyjson_doc *doc;
	yyjson_val *entries, *entry, *senses, *s;
	struct definition *d = NULL;
	struct word_class *c;
	struct buf url = {0};
	char word[MAX_WORD + 1];
	size_t i, n, k, m, shown = 0, count;

	if (f->code == CURLE_OK && f->status == 404) {
		// not a word, which is most searches. not an error
		job_done(j);
		return;
	}
	if (!job_http_ok(j, f))
		return;

	//
	// Pick the language
	//
	// the answer is keyed by language code. use the search language, or english
	//
	if (!(doc = job_json(j, f->body.p, f->body.len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	snprintf(lang, sizeof(lang), "%.*s", (int)strcspn(base, "-"), base);
	if (!strcmp(lang, "all") || !yyjson_is_arr(entries = yyjson_obj_get(yyjson_doc_get_root(doc), lang)))
		entries = yyjson_obj_get(yyjson_doc_get_root(doc), "en");
	if (!yyjson_is_arr(entries)) {
		job_done(j);
		return;
	}

	//
	// Get the definitions
	//
	snprintf(word, sizeof(word), "%s", j->query);
	yyjson_arr_foreach (entries, i, n, entry) {
		if (shown == MAX_CLASSES)
			break;
		senses = yyjson_obj_get(entry, "definitions");
		if (!yyjson_is_arr(senses) || !yyjson_arr_size(senses) ||
		    !strcmp(json_str(entry, "partOfSpeech"), "Symbol") ||
		    !strcmp(json_str(entry, "partOfSpeech"), "Proper noun"))
			continue; // names are wikipedia's job, and symbols are noise
		c = NULL;
		count = 0;
		yyjson_arr_foreach (senses, k, m, s) {
			const char *text = html_to_text(j->a, json_str(s, "definition"));

			if (!*text || count == MAX_SENSES || form_of(text))
				continue;

			// first sense we keep, make the card
			if (!d) {
				char link[MAX_WORD + 1];

				// wiktionary links use underscores for spaces
				snprintf(link, sizeof(link), "%s", word);
				for (size_t x = 0; link[x]; x++)
					if (link[x] == ' ')
						link[x] = '_';
				url_quote(&url, link, "", 0);
				d = add_definition(j, word,
				    aprintf(j->a, "https://en.wiktionary.org/wiki/%s", buf_str(&url)));
				buf_free(&url);
			}
			// first sense of this part of speech, start its list
			if (!c) {
				c = definition_class(j, d, json_str(entry, "partOfSpeech"));
				shown++;
			}
			definition_sense(j, c, text, example(j, s));
			count++;
		}
	}
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;

	if (strlen(j->query) > MAX_WORD) {
		// a sentence
		job_done(j);
		return;
	}
	url_quote(&q, j->query, "", 0);
	f = job_request(j, aprintf(j->a, "https://en.wiktionary.org/api/rest_v1/page/definition/%s", buf_str(&q)));
	buf_free(&q);
	fetch_header(f, "Accept: application/json");
	job_send(j, f, response);
}

struct engine engine_wiktionary = {
    .name = "wiktionary",
    .shortcut = "wikt",
    .website = "en.wiktionary.org",
    .bang = "https://en.wiktionary.org/wiki/%s",
    .category = "general",
    .search = search,
};
