#ifndef XML_H
#define XML_H

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include "core/util.h"

struct xnodes {
	xmlXPathObjectPtr obj;
	int n;
	xmlNodePtr *v;
};

htmlDocPtr html_parse(const char *s, size_t len);
xmlDocPtr xml_parse(const char *s, size_t len);
struct xnodes xpath(xmlNodePtr ctx, const char *expr);
void xnodes_free(struct xnodes *x);
xmlNodePtr xpath_first(xmlNodePtr ctx, const char *expr);
int xpath_exists(xmlNodePtr ctx, const char *expr);
char *node_text(struct arena *a, xmlNodePtr n);
char *xpath_text(struct arena *a, xmlNodePtr ctx, const char *expr);
char *xpath_first_text(struct arena *a, xmlNodePtr ctx, const char *expr);

#endif
