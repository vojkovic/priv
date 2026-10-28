#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libxml/parser.h>

#include "botdetection/botdetect.h"
#include "core/ev.h"
#include "core/fetch.h"
#include "core/http.h"
#include "engines/engine.h"
#include "features/favicon.h"
#include "features/proxy.h"
#include "routes.h"

int
main(void)
{
	const char *config = config_load();
	size_t i;

	if (listen_port <= 0 || listen_port > 65535)
		die("config.toml: port must be between 1 and 65535");

	setlocale(LC_CTYPE, "C.UTF-8");
	signal(SIGPIPE, SIG_IGN);
	srandom(time(NULL) ^ getpid());
	routes_init();

	xmlInitParser();
	proxy_init();
	botdetect_init();
	ev_init();
	fetch_init();
	favicon_init();
	engines_init();
	for (i = 0; i < nengines; i++)
		favicon_prefetch(FAVICON_DEFAULT, engines[i]->website);
	http_listen(listen_addr, listen_port, route);
	if (strchr(listen_addr, ':'))
		fprintf(stderr, "%s listening on http://[%s]:%d", instance_name, listen_addr, listen_port);
	else
		fprintf(stderr, "%s listening on http://%s:%d", instance_name, listen_addr, listen_port);
	fprintf(stderr, " (%s)\n", config ? config : "built-in settings");
	ev_run();
	return 0;
}
