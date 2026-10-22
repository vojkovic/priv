/* copy to config.h and rebuild to change */

const char *listen_addr = "127.0.0.1";
int listen_port = 8888;

double search_timeout = 2.0;

/* NULL: $XDG_CACHE_HOME/privy, else ~/.cache/privy */
const char *cache_dir = NULL;
int favicon_hold_days = 30;
size_t favicon_cache_bytes = 50 << 20;

/* bot detection, the same rules as searxng's limiter */
int limiter = 1;
int limiter_link_token = 0;
int limiter_filter_link_local = 0;
int ipv4_prefix = 32;
int ipv6_prefix = 48;
const char *trusted_proxies[] = { "127.0.0.0/8", "::1", 0 };
const char *pass_ip[] = { 0 };
const char *block_ip[] = { 0 };
int pass_searxng_org = 1;

int ban_time_on_fail = 5;
int max_ban_time_on_fail = 120;
int suspend_access_denied = 180;
int suspend_captcha = 3600;
int suspend_too_many_requests = 180;
int suspend_cf_captcha = 1296000;
int suspend_cf_access_denied = 86400;
int suspend_recaptcha = 604800;

const char *impersonate_chrome = "chrome150";
const char *impersonate_firefox = "firefox147";

const char *useragent_os[] = { "Windows NT 10.0; Win64; x64", "X11; Linux x86_64", 0 };
const char *useragent_versions[] = { "157.0", "156.0", 0 };

struct engine_setting engine_settings[] = {
	/* name                enabled  weight */
	{ "google",            1,       1.0 },
	{ "google cse",        1,       1.0 },
	{ "brave",             1,       1.0 },
	{ "duckduckgo",        1,       1.0 },
	{ "duckduckgo web",    1,       1.0 },
	{ "dogpile",           1,       1.0 },
	{ "privacywall",       1,       1.0 },
	{ "wikipedia",         1,       1.0 },
	{ "ddg definitions",   1,       2.0 },
	{ "google images",     1,       1.0 },
	{ "google cse images", 1,       1.0 },
	{ "brave.images",      1,       1.0 },
	{ "duckduckgo images", 1,       1.0 },
	{ "dogpile images",    1,       1.0 },
	{ "google videos",     1,       1.0 },
	{ "duckduckgo videos", 1,       1.0 },
	{ "youtube",           1,       1.0 },
	{ "brave.videos",      1,       1.0 },
	{ "google news",       1,       1.0 },
	{ "duckduckgo news",   1,       1.0 },
	{ "reuters",           1,       1.0 },
	{ "brave.news",        1,       1.0 },
	{ "soundcloud",        1,       1.0 },
	{ "wikicommons.audio", 1,       1.0 },
	{ "arch linux wiki",   1,       1.0 },
	{ "mdn",               1,       1.0 },
	{ "mankier",           1,       1.0 },
	{ "pypi",              1,       1.0 },
	{ "stackoverflow",     1,       1.0 },
	{ "askubuntu",         1,       1.0 },
	{ "superuser",         1,       1.0 },
	{ "arxiv",             1,       1.0 },
	{ "europepmc",         1,       1.0 },
	{ "pdbe",              1,       1.0 },
	{ "pubmed",            1,       1.0 },
	{ 0 },
};
