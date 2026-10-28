#include <ctype.h>
#include <string.h>

#include "engines/engine.h"

#define BASE_URL "https://api.swisscows.com"
#define SEARCH_PATH "/v5/web/search"
#define PAGE_SIZE 20

static const char *regions =
    "AR AU AT BE BR CA CL CN DK FI FR DE HK HU IN ID IT JP KR LV MY MX NL NZ NO PH PL PT RU SA ZA ES SE CH TW TR UA GB US";

static const char *
locale_of(struct job *j)
{
	const char *tag = j->filters->locale->tag, *dash = strchr(tag, '-');
	char lang[4];

	if (dash && strlen(dash + 1) == 2 && strstr(regions, dash + 1))
		return tag;
	if (dash || strlen(tag) != 2)
		return "en-US";
	lang[0] = (char)toupper((unsigned char)tag[0]);
	lang[1] = (char)toupper((unsigned char)tag[1]);
	lang[2] = 0;
	return strstr(regions, lang) ? aprintf(j->a, "%s-%s", tag, lang) : "en-US";
}

/* the payload is the middle of a json web token, in unpadded base64url */
static char *
payload(struct arena *a, const char *jwt, size_t *len)
{
	const char *p = strchr(jwt, '.'), *end;
	unsigned char *out;
	uint32_t acc = 0;
	int bits = 0, v;

	if (!p || !(end = strchr(p + 1, '.')))
		return NULL;
	p++;
	out = amalloc(a, (size_t)(end - p) * 3 / 4 + 1);
	*len = 0;
	for (; p < end; p++) {
		if (IS_ALPHA(*p))
			v = isupper((unsigned char)*p) ? *p - 'A' : *p - 'a' + 26;
		else if (IS_DIGIT(*p))
			v = *p - '0' + 52;
		else if (*p == '-' || *p == '+')
			v = 62;
		else if (*p == '_' || *p == '/')
			v = 63;
		else
			continue;
		acc = acc << 6 | (uint32_t)v;
		if ((bits += 6) >= 8)
			out[(*len)++] = (unsigned char)(acc >> (bits -= 8));
	}
	out[*len] = 0;
	return (char *)out;
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *items, *r;
	const char *data;
	size_t i, n, len;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) ||
	    !(data = payload(j->a, json_str(yyjson_doc_get_root(doc), "payload"), &len)) ||
	    !(doc = job_json(j, data, len))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	items = yyjson_obj_get(yyjson_doc_get_root(doc), "items");
	yyjson_arr_foreach (items, i, n, r)
		if (!strcmp(json_str(r, "type"), "WebPage"))
			add_main(j, json_str(r, "url"), json_str(r, "name"),
			    html_to_text(j->a, json_str(r, "description")));
	job_done(j);
}

static void
sign(struct job *j, struct fetch *f, const char *args)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
	char nonce[33], shifted[33], *sig, *p;
	unsigned char hash[32];
	struct sha256 s;
	size_t i;

	for (i = 0; i < 32; i++) {
		nonce[i] = alphabet[rnd(sizeof(alphabet) - 1)];
		shifted[i] = nonce[i];
		if (IS_ALPHA(nonce[i])) {
			char base = isupper((unsigned char)nonce[i]) ? 'A' : 'a';
			shifted[i] = (char)((nonce[i] - base + 13) % 26 + base);
			shifted[i] = isupper((unsigned char)shifted[i]) ? (char)tolower((unsigned char)shifted[i]) :
			                                                  (char)toupper((unsigned char)shifted[i]);
		}
	}
	nonce[32] = shifted[32] = 0;
	sha256_init(&s);
	sha256_update(&s, SEARCH_PATH "?", strlen(SEARCH_PATH "?"));
	sha256_update(&s, args, strlen(args));
	sha256_update(&s, shifted, 32);
	sha256_final(&s, hash);
	sig = base64(j->a, hash, sizeof(hash));
	for (p = sig; *p; p++)
		*p = *p == '+' ? '-' : *p == '/' ? '_' : *p;
	if ((p = strchr(sig, '=')))
		*p = 0;
	fetch_header(f, "X-Request-Nonce: %s", nonce);
	fetch_header(f, "X-Request-Signature: %s", sig);
}

static void
search(struct job *j)
{
	static const char *ranges[][2] = {{"day", "Day"}, {"week", "Week"}, {"month", "Month"}, {"year", "Year"}};
	const char *freshness = "All", *locale = locale_of(j),
	           *offset = aprintf(j->a, "%d", (j->pageno - 1) * PAGE_SIZE);
	struct buf q = {0};
	struct fetch *f;
	size_t i;

	for (i = 0; j->filters->time_range && i < LEN(ranges); i++)
		if (!strcmp(j->filters->time_range, ranges[i][0]))
			freshness = ranges[i][1];
	form_add(&q, "freshness", freshness);
	form_add(&q, "itemsCount", aprintf(j->a, "%d", PAGE_SIZE));
	form_add(&q, "locale", locale);
	form_add(&q, "offset", offset);
	form_add(&q, "query", j->query);
	form_add(&q, "spellcheck", "True");
	f = job_request(j, aprintf(j->a, BASE_URL SEARCH_PATH "?%s", buf_str(&q)));
	buf_free(&q);
	sign(j, f,
	    aprintf(j->a, "freshness=%s&itemsCount=%d&locale=%s&offset=%s&query=%s&spellcheck=True", freshness,
	        PAGE_SIZE, locale, offset, j->query));
	job_send(j, f, response);
}

struct engine engine_swisscows = {
    .name = "swisscows",
    .shortcut = "sw",
    .website = "swisscows.com",
    .bang = "https://swisscows.com/en/web?query=%s",
    .category = "general",
    .paging = 1,
    .time_range = 1,
    .search = search,
};
