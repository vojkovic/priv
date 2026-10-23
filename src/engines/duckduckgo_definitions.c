#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../engine.h"

static const struct {
	const char *id, *url;
} external_urls[] = {
    {"facebook_profile", "https://facebook.com/$1"},
    {"youtube_channel", "https://www.youtube.com/channel/$1"},
    {"youtube_video", "https://www.youtube.com/watch?v=$1"},
    {"twitter_profile", "https://twitter.com/$1"},
    {"instagram_profile", "https://www.instagram.com/$1"},
    {"imdb_title", "https://www.imdb.com/title/$1"},
    {"imdb_name", "https://www.imdb.com/name/$1"},
    {"imdb_character", "https://www.imdb.com/character/$1"},
    {"imdb_company", "https://www.imdb.com/company/$1"},
    {"imdb_event", "https://www.imdb.com/event/$1"},
    {"rotten_tomatoes", "https://www.rottentomatoes.com/$1"},
    {"spotify_artist_id", "https://open.spotify.com/artist/$1"},
    {"itunes_artist_id", "https://music.apple.com/us/artist/$1"},
    {"soundcloud_id", "https://soundcloud.com/$1"},
    {"netflix_id", "https://www.netflix.com/watch/$1"},
    {"github_profile", "https://wwww.github.com/$1"},
    {"musicbrainz_artist", "http://musicbrainz.org/artist/$1"},
    {"musicbrainz_work", "http://musicbrainz.org/work/$1"},
    {"musicbrainz_release_group", "http://musicbrainz.org/release-group/$1"},
    {"musicbrainz_label", "http://musicbrainz.org/label/$1"},
    {"wikimedia_image", "https://commons.wikimedia.org/wiki/Special:FilePath/$1?width=500&height=400"},
};

static const struct {
	const char *entity, *symbol;
} area_units[] = {
    {"Q712226", "km\xc2\xb2"},
    {"Q25343", "m\xc2\xb2"},
    {"Q232291", "mi\xc2\xb2"},
    {"Q35852", "ha"},
    {"Q81292", "acre"},
    {"Q23931103", "nmi\xc2\xb2"},
    {"Q857027", "ft\xc2\xb2"},
};

static char *
py_float(struct arena *a, double d)
{
	char buf[64];
	int prec;

	for (prec = 1; prec <= 17; prec++) {
		snprintf(buf, sizeof(buf), "%.*g", prec, d);
		if (strtod(buf, NULL) == d)
			break;
	}
	if (!strpbrk(buf, ".eEn"))
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), ".0");
	return astrdup(a, buf);
}

static char *
external_url(struct arena *a, const char *id, const char *item)
{
	static const char *imdb[][2] = {
	    {"tt", "imdb_title"},
	    {"mn", "imdb_name"},
	    {"ch", "imdb_character"},
	    {"co", "imdb_company"},
	    {"ev", "imdb_event"},
	};
	const char *t;
	size_t i;

	if (!strcmp(id, "imdb_id")) {
		for (i = 0, id = ""; i < LEN(imdb); i++)
			if (!strncmp(item, imdb[i][0], 2))
				id = imdb[i][1];
	} else if (!strcmp(id, "wikimedia_image")) {
		if (starts_with(item, "http://commons.wikimedia.org/wiki/Special:FilePath/"))
			item += 51;
		else if (starts_with(item, "File:"))
			item += 5;
	}
	for (i = 0; i < LEN(external_urls); i++) {
		if (strcmp(external_urls[i].id, id))
			continue;
		t = strstr(external_urls[i].url, "$1");
		return aprintf(a, "%.*s%s%s", (int)(t - external_urls[i].url), external_urls[i].url, item, t + 2);
	}
	return NULL;
}

static const char *
unit_to_str(const char *unit)
{
	const char *ent = NULL;
	size_t i;

	if (starts_with(unit, "http://www.wikidata.org/entity/"))
		ent = unit + 31;
	else if (starts_with(unit, "https://www.wikidata.org/entity/"))
		ent = unit + 32;
	if (!ent)
		return unit;
	for (i = 0; i < LEN(area_units); i++)
		if (!strcmp(area_units[i].entity, ent))
			return area_units[i].symbol;
	return unit;
}

static char *
area_to_str(struct arena *a, yyjson_val *area)
{
	const char *unit = unit_to_str(json_str(area, "unit")), *amount = json_str(area, "amount");
	char *end;
	double d;

	if (*unit) {
		d = strtod(amount, &end);
		if (*amount && !*end)
			return aprintf(a, "%s %s", py_float(a, d), unit);
	}
	return aprintf(a, "%s %s", amount, json_str(area, "unit"));
}

static int
area_to_osm_zoom(const char *amount)
{
	char *end;
	double d = strtod(amount, &end), z;

	if (!*amount || *end || d <= 0)
		return 19;
	z = rint(19 - 0.688297 * log(226.878 * d));
	return z < 0 ? 0 : z > 19 ? 19 : (int)z;
}

static void
response(struct job *j, struct fetch *f)
{
	yyjson_doc *doc;
	yyjson_val *root, *item, *info, *val, *coords = NULL;
	const char *heading, *image, *first, *text, *abstract_url, *def_url, *id = NULL;
	struct buf content = {0};
	struct infobox tmp = {0}, *ib;
	struct infobox_url *iu;
	size_t i, n;
	int related = 0, osm_zoom = 17, nurls = 0;
	struct url u;

	if (!job_http_ok(j, f))
		return;
	if (!(doc = job_json(j, f->body.p, f->body.len)) || !yyjson_is_obj(root = yyjson_doc_get_root(doc))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	heading = json_str(root, "Heading");

	buf_puts(&content, json_str(root, "Definition"));
	buf_puts(&content, json_str(root, "Abstract"));

	image = json_str(root, "Image");
	if (*image) {
		url_parse(j->a, image, &u);
		if (!*u.netloc)
			image = url_join(j->a, "https://duckduckgo.com", image);
	} else
		image = NULL;

	yyjson_arr_foreach (yyjson_obj_get(root, "Results"), i, n, item) {
		first = yyjson_get_str(yyjson_obj_get(item, "FirstURL"));
		text = yyjson_get_str(yyjson_obj_get(item, "Text"));
		if (first && text) {
			infobox_url(j, &tmp, text, first, NULL, 0);
			add_main(j, first, heading, "");
		}
	}

	yyjson_arr_foreach (yyjson_obj_get(root, "RelatedTopics"), i, n, item)
		if (!yyjson_obj_get(item, "FirstURL") && yyjson_obj_get(item, "Topics"))
			related++;

	abstract_url = json_str(root, "AbstractURL");
	if (*abstract_url) {
		id = abstract_url;
		infobox_url(j, &tmp, json_str(root, "AbstractSource"), abstract_url, NULL, 1);
		add_main(j, abstract_url, heading, "");
	}
	def_url = json_str(root, "DefinitionURL");
	if (*def_url) {
		id = def_url;
		infobox_url(j, &tmp, json_str(root, "DefinitionSource"), def_url, NULL, 0);
	}
	if (id && starts_with(id, "http:"))
		id = aprintf(j->a, "https:%s", id + 5);

	yyjson_arr_foreach (yyjson_obj_get(yyjson_obj_get(root, "Infobox"), "content"), i, n, info) {
		const char *type = json_str(info, "data_type"), *label = json_str(info, "label");
		char *ext;
		val = yyjson_obj_get(info, "value");
		if (yyjson_is_str(val) && !strcmp(yyjson_get_str(val), "\"\""))
			continue;
		if (yyjson_is_str(val) && (ext = external_url(j->a, type, yyjson_get_str(val))))
			infobox_url(j, &tmp, label, ext, NULL, 0);
		else if (!strcmp(type, "instance") || !strcmp(type, "wiki_maps_trigger") ||
		    !strcmp(type, "google_play_artist_id") || (!strcmp(type, "string") && !strcmp(label, "Website")))
			;
		else if (!strcmp(type, "area")) {
			infobox_attr(j, &tmp, label, area_to_str(j->a, val), "P2046");
			osm_zoom = area_to_osm_zoom(json_str(val, "amount"));
		} else if (!strcmp(type, "coordinates")) {
			if (!strcmp(json_str(val, "globe"), "http://www.wikidata.org/entity/Q2"))
				coords = info;
			else {
				char *js = yyjson_val_write(val, 0, NULL);
				infobox_attr(j, &tmp, label, js, "P625");
				free(js);
			}
		} else if (!strcmp(type, "string") && yyjson_is_str(val))
			infobox_attr(j, &tmp, label, yyjson_get_str(val), NULL);
	}
	if (coords) {
		val = yyjson_obj_get(coords, "value");
		infobox_url(j, &tmp, "OpenStreetMap",
		    aprintf(j->a, "https://www.openstreetmap.org/?lat=%s&lon=%s&zoom=%d&layers=M",
		        py_float(j->a, yyjson_get_num(yyjson_obj_get(val, "latitude"))),
		        py_float(j->a, yyjson_get_num(yyjson_obj_get(val, "longitude"))), osm_zoom),
		    "P625", 0);
	}

	for (iu = tmp.urls; iu; iu = iu->next)
		nurls++;
	if (*heading) {
		if (!image && !tmp.attrs && nurls == 1 && !related && !content.len) {
			add_main(j, tmp.urls->url, heading, "");
		} else {
			ib = add_infobox(j, heading);
			ib->id = id ? astrdup(j->a, id) : NULL;
			ib->content = astrdup(j->a, buf_str(&content));
			ib->img_src = image ? astrdup(j->a, image) : NULL;
			ib->attrs = tmp.attrs;
			ib->urls = tmp.urls;
		}
	}
	buf_free(&content);
	job_done(j);
}

static void
search(struct job *j)
{
	struct buf q = {0};

	form_add(&q, "q", j->query);
	job_send(j,
	    job_request(j,
	        aprintf(j->a, "https://api.duckduckgo.com/?%s&format=json&pretty=0&no_redirect=1&d=1", buf_str(&q))),
	    response);
	buf_free(&q);
}

struct engine engine_duckduckgo_definitions = {
    .name = "ddg definitions",
    .shortcut = "ddd",
    .weight = 2.0,
    .website = "duckduckgo.com",
    .categories = {"general"},
    .search = search,
};
