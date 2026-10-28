#include "engines/engine.h"

#define EUTILS "https://eutils.ncbi.nlm.nih.gov/entrez/eutils"
#define PAGE_SIZE 10

//
// PubMed, in two steps
//
// 1. esearch finds the ids of the papers that match (search, ids_response)
// 2. efetch turns the ids into the papers themselves, as xml (response)
//

static void
response(struct job *j, struct fetch *f)
{
	struct xnodes articles, names;
	struct buf authors = {0};
	struct main_result *m;
	xmlNodePtr c, author;
	xmlDocPtr doc;
	char *y, *mo, *d, *name;
	int i, k;

	if (!job_http_ok(j, f))
		return;

	//
	// Get results
	//
	if (!(doc = job_xml(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	articles = xpath((xmlNodePtr)doc, "//PubmedArticle");
	for (i = 0; i < articles.n; i++) {
		if (!(c = xpath_first(articles.v[i], "./MedlineCitation")))
			continue;
		m = add_paper(j,
		    aprintf(j->a, "https://www.ncbi.nlm.nih.gov/pubmed/%s", xpath_text(j->a, c, ".//PMID")),
		    xpath_text(j->a, c, ".//Article/ArticleTitle"), xpath_text(j->a, c, ".//Abstract/AbstractText"));
		buf_free(&authors);
		names = xpath(c, "./Article/AuthorList/Author");
		for (k = 0; k < names.n; k++) {
			author = names.v[k];
			name = str_strip(aprintf(j->a, "%s %s", xpath_text(j->a, author, "./ForeName"),
			    xpath_text(j->a, author, "./LastName")));
			if (*name)
				buf_printf(&authors, "%s%s", authors.len ? ", " : "", name);
		}
		xnodes_free(&names);
		result_attr(j, m, "Author", buf_str(&authors));
		result_attr(j, m, "Journal", xpath_text(j->a, c, "./Article/Journal/Title"));
		result_attr(j, m, "DOI", xpath_text(j->a, c, ".//ELocationID[@EIdType='doi']"));
		result_attr(j, m, "ISSN", xpath_text(j->a, c, "./Article/Journal/ISSN"));
		// the date the paper was accepted, only when all three parts are there
		y = xpath_text(j->a, articles.v[i], "./PubmedData/History//PubMedPubDate[@PubStatus='accepted']/Year");
		mo =
		    xpath_text(j->a, articles.v[i], "./PubmedData/History//PubMedPubDate[@PubStatus='accepted']/Month");
		d = xpath_text(j->a, articles.v[i], "./PubmedData/History//PubMedPubDate[@PubStatus='accepted']/Day");
		if (*y && *mo && *d)
			m->pubdate = (char *)format_date(j->a,
			    aprintf(j->a, "%04ld-%02ld-%02ld", parse_long(y, 0), parse_long(mo, 0), parse_long(d, 0)));
	}
	buf_free(&authors);
	xnodes_free(&articles);
	job_done(j);
}

static void
ids_response(struct job *j, struct fetch *f)
{
	struct xnodes ids;
	struct buf q = {0};
	xmlDocPtr doc;
	int i;

	if (!job_http_ok(j, f))
		return;

	//
	// Get the ids, then ask for the papers
	//
	if (!(doc = job_xml(j, f))) {
		job_fail(j, "parsing error", SUSPEND_NONE);
		return;
	}
	ids = xpath((xmlNodePtr)doc, "//eSearchResult/IdList/Id");
	buf_puts(&q, "db=pubmed&retmode=xml&id=");
	for (i = 0; i < ids.n; i++)
		buf_printf(&q, "%s%s", i ? "%2C" : "", node_text(j->a, ids.v[i]));
	xnodes_free(&ids);
	f = job_request(j, aprintf(j->a, EUTILS "/efetch.fcgi?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, response);
}

static void
search(struct job *j)
{
	struct buf q = {0};
	struct fetch *f;
	char start[16];

	snprintf(start, sizeof(start), "%d", (j->pageno - 1) * PAGE_SIZE);
	form_add(&q, "db", "pubmed");
	form_add(&q, "term", j->query);
	form_add(&q, "retstart", start);
	form_add(&q, "hits", "10");
	f = job_request(j, aprintf(j->a, EUTILS "/esearch.fcgi?%s", buf_str(&q)));
	buf_free(&q);
	job_send(j, f, ids_response);
}

struct engine engine_pubmed = {
    .name = "pubmed",
    .shortcut = "pub",
    .website = "pubmed.ncbi.nlm.nih.gov",
    .bang = "https://pubmed.ncbi.nlm.nih.gov/?term=%s",
    .category = "research",
    .search = search,
};
