#include <string.h>

#include "xml.h"

htmlDocPtr
html_parse(const char *s, size_t len)
{
	return htmlReadMemory(s, (int)len, NULL, "UTF-8",
	    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_COMPACT);
}

xmlDocPtr
xml_parse(const char *s, size_t len)
{
	return xmlReadMemory(s, (int)len, NULL, "UTF-8",
	    XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET | XML_PARSE_COMPACT);
}

struct xnodes
xpath(xmlNodePtr ctx, const char *expr)
{
	struct xnodes x = {0};
	xmlXPathContextPtr xc;

	if (!ctx)
		return x;
	xc = xmlXPathNewContext(ctx->doc ? ctx->doc : (xmlDocPtr)ctx);
	if (!xc)
		return x;
	xc->node = ctx;
	x.obj = xmlXPathEvalExpression((const xmlChar *)expr, xc);
	xmlXPathFreeContext(xc);
	if (x.obj && x.obj->type == XPATH_NODESET && x.obj->nodesetval) {
		x.n = x.obj->nodesetval->nodeNr;
		x.v = x.obj->nodesetval->nodeTab;
	}
	return x;
}

void
xnodes_free(struct xnodes *x)
{
	if (x->obj)
		xmlXPathFreeObject(x->obj);
	memset(x, 0, sizeof(*x));
}

xmlNodePtr
xpath_first(xmlNodePtr ctx, const char *expr)
{
	struct xnodes x = xpath(ctx, expr);
	xmlNodePtr n = x.n ? x.v[0] : NULL;

	xnodes_free(&x);
	return n;
}

int
xpath_exists(xmlNodePtr ctx, const char *expr)
{
	return xpath_first(ctx, expr) != NULL;
}

static char *
raw_text(struct arena *a, xmlNodePtr n)
{
	xmlChar *c = xmlNodeGetContent(n);
	char *r = astrdup(a, c ? (char *)c : "");

	xmlFree(c);
	return r;
}

char *
node_text(struct arena *a, xmlNodePtr n)
{
	if (!n)
		return NULL;
	if (n->type == XML_ELEMENT_NODE || n->type == XML_DOCUMENT_NODE || n->type == XML_HTML_DOCUMENT_NODE)
		return text_split_join(a, raw_text(a, n));
	return raw_text(a, n);
}

char *
xpath_text(struct arena *a, xmlNodePtr ctx, const char *expr)
{
	struct xnodes x = xpath(ctx, expr);
	struct buf b = {0};
	char *r;
	int i;

	if (x.obj && x.obj->type == XPATH_STRING) {
		r = astrdup(a, (char *)x.obj->stringval);
		xnodes_free(&x);
		return r;
	}
	for (i = 0; i < x.n; i++)
		buf_puts(&b, node_text(a, x.v[i]));
	xnodes_free(&x);
	r = py_strip(astrdup(a, buf_str(&b)));
	buf_free(&b);
	return r;
}

char *
xpath_first_text(struct arena *a, xmlNodePtr ctx, const char *expr)
{
	return node_text(a, xpath_first(ctx, expr));
}
