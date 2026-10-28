#include "core/util.h"
#include <ctype.h>
#include <errno.h>
#include <libxml/HTMLparser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <wctype.h>

//
// Time, random numbers and a browser string
//

double
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

double
wall_time(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

uint32_t
rnd(uint32_t n)
{
	return n ? (uint32_t)random() % n : 0;
}

char *
gen_useragent(void)
{
	size_t nos = 0, nver = 0;
	const char *os, *ver;
	char buf[256];

	while (useragent_os[nos])
		nos++;
	while (useragent_versions[nver])
		nver++;
	os = useragent_os[rnd(nos)];
	ver = useragent_versions[rnd(nver)];
	snprintf(buf, sizeof(buf), "Mozilla/5.0 (%s; rv:%s) Gecko/20100101 Firefox/%s", os, ver, ver);
	return xstrdup(buf);
}
