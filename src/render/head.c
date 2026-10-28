#include "botdetection/botdetect.h"
#include "engines/engine.h"
#include "features/autocomplete.h"
#include "features/favicon.h"
#include "features/proxy.h"
#include "features/stats.h"
#include "render/icons.h"
#include "render/internal.h"
#include "render/render.h"
#include "version.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

//
// The top of a results page
//
// this goes out before the engines answer, so it can only depend on the query.
// that is why instant answers are decided here, from the text alone: the
// query is checked for the calculator, a unit conversion or a colour, and the
// matching script is added to the page. the self info card is the one answer
// built on the server.
//

/* whether a query reads as arithmetic, which loads the calculator; calc.js does the real parsing */
static int
math_query(const char *q)
{
	static const char *const fillers[] = {"what", "is", "whats", "calculate", "calc", "compute", "solve", "equals",
	    "to", "the", "by"};
	static const char *const constants[] = {"pi", "e", "tau", "phi", "ans"};
	static const char *const operations[] = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh",
	    "tanh", "asinh", "acosh", "atanh", "arcsin", "arccos", "arctan", "sqrt", "cbrt", "cuberoot", "root", "abs",
	    "sign", "sgn", "floor", "ceil", "ceiling", "trunc", "round", "exp", "ln", "log", "log2", "log10", "lg",
	    "pow", "hypot", "min", "max", "mod", "modulo", "gcd", "lcm", "comb", "perm", "ncr", "npr", "factorial",
	    "fact", "radians", "degrees", "deg", "rad", "of", "percent", "cent", "per", "plus", "minus", "times",
	    "multiplied", "divided", "over", "squared", "cubed", "power", "raised"};
	/* UTF-8 for the times, divide, minus, dot, power, degree, pi and root signs */
	static const char *const symbols[] = {"\xc3\x97", "\xc3\xb7", "\xe2\x88\x92", "\xe2\x80\x93", "\xe2\x80\x94",
	    "\xc2\xb7", "\xe2\x8b\x85", "\xe2\x88\x97", "\xc2\xb2", "\xc2\xb3", "\xc2\xb0", "\xcf\x80", "\xe2\x88\x9a",
	    "\xe2\x88\x9b"};
	size_t n = strlen(q), i, k, len;
	int operators = 0, operands = 0, hyphens = 0, dashed = 1;
	char word[16];

	if (!n || n > 200)
		return 0;
	for (i = 0; i < n;) {
		if (IS_DIGIT(q[i])) {
			operands = 1;
			i++;
		} else if (IS_ALPHA(q[i])) {
			for (len = 0; IS_ALPHA(q[i + len]) || IS_DIGIT(q[i + len]); len++)
				if (len >= sizeof(word) - 1)
					return 0;
			memcpy(word, q + i, len);
			word[len] = 0;
			i += len;
			dashed = 0;
			if (i > len && IS_DIGIT(q[i - len - 1]) && (word[0] | 0x20) == 'e' &&
			    strspn(word + 1, "0123456789") == len - 1)
				continue;
			for (k = 0; k < LEN(operations) && strcasecmp(word, operations[k]); k++)
				;
			if (k < LEN(operations)) {
				operators = 1;
				continue;
			}
			for (k = 0; k < LEN(constants) && strcasecmp(word, constants[k]); k++)
				;
			if (k < LEN(constants)) {
				operands = 1;
				continue;
			}
			for (k = 0; k < LEN(fillers) && strcasecmp(word, fillers[k]); k++)
				;
			if (k == LEN(fillers))
				return 0;
		} else if (strchr(" \t.,'=()", q[i])) {
			dashed = 0;
			i++;
		} else if (strchr("+-*/^%!", q[i])) {
			operators = 1;
			hyphens += q[i] == '-';
			dashed &= q[i] == '-';
			i++;
		} else {
			for (k = 0; k < LEN(symbols) && strncmp(q + i, symbols[k], strlen(symbols[k])); k++)
				;
			if (k == LEN(symbols))
				return 0;
			operators = 1;
			operands |= k == 11;
			dashed = 0;
			i += strlen(symbols[k]);
		}
	}
	/* 555-1234-9876 and 2024-10-25 are phone numbers and dates */
	return operators && operands && !(dashed && hyphens >= 2);
}

/* whether a query could be a unit conversion such as "5 km to miles"; calc.js checks the units */
static int
unit_query(const char *q)
{
	static const char *const connectors[] = {"to", "in", "into", "as", "equals", "->", "=>", "="};
	size_t n = strlen(q), i = 0, len, k, words = 0, joint = 0;
	int digits = 0, found = 0, asking;
	char word[8];

	if (!n || n > 120)
		return 0;
	asking = !strncasecmp(q, "convert ", 8) || !strncasecmp(q, "how many ", 9) || !strncasecmp(q, "how much ", 9) ||
	    !strncasecmp(q, "what is ", 8);
	while (i < n) {
		while (q[i] == ' ' || q[i] == '\t')
			i++;
		if (i >= n)
			break;
		len = strcspn(q + i, " \t");
		for (k = 0; k < len; k++)
			digits |= IS_DIGIT(q[i + k]);
		if (len < sizeof(word) && !found && words) {
			memcpy(word, q + i, len);
			word[len] = 0;
			for (k = 0; k < LEN(connectors); k++)
				if (!strcasecmp(word, connectors[k])) {
					found = 1;
					joint = words;
				}
		}
		words++;
		i += len;
	}
	return found && joint < words - 1 && words >= 3 && words <= 10 && (digits || asking || words <= 4);
}

/* whether a query is a number and a unit, such as "38 oz"; calc.js checks the unit */
static int
quantity_query(const char *q)
{
	size_t n = strlen(q), i, words = 1;

	if (!n || n > 40)
		return 0;
	for (i = 0; i < n && (IS_DIGIT(q[i]) || strchr(".,/-", q[i])); i++)
		;
	if (i == n || !IS_DIGIT(q[0]) || (q[i] != ' ' && !IS_ALPHA(q[i]) && (unsigned char)q[i] < 0x80))
		return 0;
	for (; i < n; i++)
		words += q[i] == ' ';
	return words <= 3 && (IS_ALPHA(q[n - 1]) || (unsigned char)q[n - 1] >= 0x80);
}

/* whether a query asks for a colour, such as "color picker", "#f0f0f0" or "rgb 255 255 255"; color.js reads it */
static int
color_query(const char *q)
{
	static const char *const phrases[] = {"color picker", "colour picker", "colorpicker", "colourpicker",
	    "hex to rgb", "rgb to hex", "hex to hsl", "hsl to hex", "rgb to hsl", "hsl to rgb", "rgb to hsv",
	    "hex to hsv", "rgb to cmyk", "hex to cmyk", "html color", "color converter", "colour converter",
	    "color code", "colour code"};
	static const char *const notations[] = {"rgb", "rgba", "hsl", "hsla", "hsv", "hsb", "cmyk"};
	size_t n = strlen(q), i, len;

	if (!n || n > 80)
		return 0;
	while (*q == ' ')
		q++;
	for (i = 0; i < LEN(phrases); i++)
		if (strcasestr(q, phrases[i]))
			return 1;
	if (*q == '#') {
		len = strspn(q + 1, "0123456789abcdefABCDEF");
		return (len == 3 || len == 4 || len == 6 || len == 8) && (!q[len + 1] || q[len + 1] == ' ');
	}
	if (!strncasecmp(q, "color ", 6) || !strncasecmp(q, "colour ", 7) || !strncasecmp(q, "hex ", 4)) {
		q += strcspn(q, " ") + 1;
		len = strspn(q, "0123456789abcdefABCDEF");
		return (len == 3 || len == 4 || len == 6 || len == 8) && !q[len];
	}
	for (i = 0; i < LEN(notations); i++) {
		len = strlen(notations[i]);
		if (!strncasecmp(q, notations[i], len)) {
			q += len;
			q += strspn(q, " (");
			return IS_DIGIT(*q) || ((*q == '-' || *q == '.') && IS_DIGIT(q[1]));
		}
	}
	return 0;
}

/* whether a query asks for the visitor's own connection details */
int
self_query(const char *q)
{
	static const char *const asks[] = {"ip", "myip", "whatismyip", "my ip", "ip address", "my ip address",
	    "what is my ip", "what is my ip address", "whats my ip", "whats my ip address", "whats my user agent",
	    "what is my user agent", "user agent", "my user agent", "my browser", "what is my browser", "my language",
	    "my browser language", "what is my language"};
	char norm[64], *d = norm;
	int space = 0;
	size_t i;

	for (; *q && d < norm + sizeof(norm) - 2; q++) {
		if (*q == '\'' || *q == '?' || *q == '.')
			continue;
		if (*q == ' ') {
			space = d > norm;
			continue;
		}
		if (space)
			*d++ = ' ';
		space = 0;
		*d++ = (char)tolower((unsigned char)*q);
	}
	*d = 0;
	for (i = 0; i < LEN(asks); i++)
		if (!strcmp(norm, asks[i]))
			return 1;
	return 0;
}

void
selfinfo(struct buf *b, struct filters *f)
{
	buf_html(b,
	    "<section id=\"selfinfo\" aria-label=\"%s\">"
	    "<div class=\"self-label\">%s</div>"
	    "<div class=\"self-ip\"><strong%r>%s</strong><span>%s</span></div>"
	    "<dl><dt>%s</dt><dd>%s</dd><dt>%s</dt><dd>%s</dd></dl>"
	    "</section>\n",
	    tr("Your connection"), tr("IP address"), strlen(f->client_ip) > 15 ? " class=\"long\"" : "", f->client_ip,
	    strchr(f->client_ip, ':') ? "IPv6" : "IPv4", tr("User agent"), f->client_agent, tr("Languages"),
	    f->client_language);
}

void
render_head(struct buf *b, struct search *s)
{
	struct buf page = {0};
	int seconds = (int)(filters_timeout(&s->filters) + 0.99);

	i18n_use(s->filters.prefs.ui);
	put_tr(&page, "Page {0}", aprintf(&s->a, "%d", s->pageno), "");
	doc_start(b,
	    s->pageno > 1 ? aprintf(&s->a, "%s - %s - %s", s->raw, buf_str(&page), instance_name) :
	                    aprintf(&s->a, "%s - %s", s->raw, instance_name),
	    "results_endpoint", &s->filters.prefs);
	buf_free(&page);
	buf_html(b,
	    "<header id=\"search_header\">\n"
	    "<a id=\"search_logo\" href=\"/\" aria-label=\"%s\">" LOGO "</a>\n"
	    "<form id=\"search\" action=\"/search\" method=\"get\" role=\"search\">\n",
	    instance_name);
	search_box(b, s->raw, s->filters.category, 0);
	category_tabs(b, s->filters.category);
	buf_puts(b, "</form>\n");
	nav_links(b);
	buf_html(b,
	    "</header>\n"
	    "<div class=\"loading t%d\" role=\"progressbar\" aria-label=\"%s\"></div>\n",
	    MIN(MAX(seconds, 1), 10), tr("Searching"));
	search_filters(b, &s->filters);
	buf_html(b, "<div id=\"results\" class=\"category-%s\">\n", s->filters.category);
	if (s->pageno == 1 && !strcmp(s->filters.category, "general")) {
		if (color_query(s->raw))
			buf_puts(b, "<script src=\"/color.js\"></script>\n");
		else if (math_query(s->raw) || unit_query(s->raw) || quantity_query(s->raw))
			buf_puts(b, "<script src=\"/calc.js\"></script>\n");
	}
}
