#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "features/urlclean.h"

/* remove trackers from links */
struct site {
	const char *host, *params;
};

static const struct site sites[] = {
    {"",
        "utm_* mtm_* ga_* otm_* itm_campaign itm_medium itm_source yclid _openstat fb_action_ids fb_action_types "
        "fb_source fb_ref action_object_map action_type_map action_ref_map gs_l mkt_tok hmb_campaign hmb_medium "
        "hmb_source gclid srsltid msockid cmpid os_ehash _ga _gl __twitter_impression wtrid dclid echobox spm "
        "vn_* tracking_source ceneo_spo __hsfp __hssc __hstc _hsenc _hsmi __s hsctatracking mc_eid mc_cid mc_tc "
        "ml_subscriber ml_subscriber_hash msclkid oly_anon_id oly_enc_id rb_clickid s_cid vero_conv vero_id "
        "wickedid twclid fbclid"},
    {"amazon",
        "pf_rd_* pd_rd_* qid sr srs __mk_* spia ms3_c ie refrid colid coliid qualifier _encoding smid "
        "field-lbr_brands_browse-bin ref ref_ th sprefix crid cv_ct_* linkcode creativeasin ascsubtag aaxitk "
        "hsa_cr_id sb-ci-* rnid dchild camp creative content-id dib dib_tag social_share starsleft skiptwisterog "
        "tag"},
    {"google",
        "ved ei oq esrc uact atyp vet _u je dcr sei sa dpr usg aqs sourceid sxsrf rlz pcampaignid sca_esv sca_upv "
        "iflsig fbs ictx cshid biw bih gs_* gws_* gfe_*"},
    {"youtube.com", "feature gclid kw si pp"},
    {"youtu.be", "si feature"},
    {"facebook.com",
        "hc_* fref ref refsrc __tn__ __cft__* __xts__* comment_tracking ls_ref action_history referral_code "
        "referral_story_type eav sfnsn wtsid rdc rdr paipv _nc_x _rdr mibextid"},
    {"twitter.com", "src ref_src refsrc s cn ref_url t"},
    {"x.com", "src ref_src refsrc s cn ref_url t"},
    {"reddit.com",
        "$deep_link %24deep_link correlation_id ref_campaign ref_source $3p %243p rdt $original_url "
        "%24original_url _branch_match_id share_id"},
    {"linkedin.com", "refid trk lipi licu trackingid"},
    {"tiktok.com", "u_code preview_pb _d _t _r timestamp user_id share_app_name share_iid source"},
    {"instagram.com", "igshid igsh"},
    {"ebay", "_trkparms _trksid _from hash"},
    {"aliexpress",
        "ws_ab_test btsid algo_expid algo_pvid gps-id scm* cv af mall_affr sk dp terminal_id aff_request_id"},
    {"spotify.com", "si"},
    {"bing.com", "cvid sk sp sc qs qp form"},
    {"msn.com", "cvid ocid"},
    {"netflix.com", "trackid tctx jb*"},
    {"imdb.com", "ref_ pf_rd_*"},
    {"twitch", "tt_medium tt_content"},
    {"medium.com", "source"},
    {"github.com", "email_token email_source"},
    {"etsy.com", "click_key click_sum organic_search_click"},
    {"walmart.com", "u1 ath*"},
    {"nytimes.com", "smid"},
    {"theguardian.com", "cmp"},
    {"bbc", "xtor at_*"},
    {"apple.com", "ign-itsc*"},
    {"cnet.com", "ftag"},
    {"wired.com", "intcid"},
    {"forbes.com", "sh"},
    {"techcrunch.com", "ncid sr sr_share guccounter guce_referrer guce_referrer_sig"},
    {"goodreads.com", "from_search from_srp qid rank ac"},
    {"reuters.com", "taid"},
    {"newyorker.com", "source bxid cndid esrc mbid"},
    {"snapchat.com", "sc_referrer sc_ua"},
    {"steampowered.com", "snr"},
    {"bestbuy.com", "irclickid irgwc loc acampid mpid intl"},
};

struct hop {
	const char *host, *path, *param;
};

static const struct hop hops[] = {
    {"google", "/url", "url"},
    {"google", "/url", "q"},
    {"duckduckgo.com", "/l/", "uddg"},
    {"facebook.com", "/l.php", "u"},
    {"youtube.com", "/redirect", "q"},
    {"out.reddit.com", "", "url"},
    {"click.redditmail.com", "", "url"},
    {"steamcommunity.com", "/linkfilter/", "url"},
    {"vk.com", "/away.php", "to"},
    {"rover.ebay", "/rover", "mpre"},
};

#define MAX_HOPS 3

static char *
hostname(struct arena *a, const char *netloc)
{
	char *h = astrdup(a, strrchr(netloc, '@') ? strrchr(netloc, '@') + 1 : netloc), *p;

	if (*h != '[' && (p = strrchr(h, ':')))
		*p = 0;
	for (p = h; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return h;
}

static int
host_is(const char *host, const char *rule)
{
	size_t n = strlen(host), k = strlen(rule);
	const char *p;

	if (!k)
		return 1;
	if (strchr(rule, '.'))
		return n >= k && !strcmp(host + n - k, rule) && (n == k || host[n - k - 1] == '.');
	for (p = host; p && *p; p = strchr(p, '.') ? strchr(p, '.') + 1 : NULL)
		if (!strncmp(p, rule, k) && (p[k] == '.' || !p[k]))
			return 1;
	return 0;
}

static int
name_listed(const char *params, const char *name, size_t n)
{
	const char *p = params, *end;
	size_t k;

	while (*p) {
		end = p + strcspn(p, " ");
		k = (size_t)(end - p);
		if (k && p[k - 1] == '*' ? n >= k - 1 && !strncasecmp(p, name, k - 1) :
		                           n == k && !strncasecmp(p, name, n))
			return 1;
		p = *end ? end + 1 : end;
	}
	return 0;
}

static int
tracker(const char *host, const char *pair, size_t n)
{
	size_t i, name = strcspn(pair, "=&");

	if (name > n)
		name = n;
	for (i = 0; i < LEN(sites); i++)
		if (host_is(host, sites[i].host) && name_listed(sites[i].params, pair, name))
			return 1;
	return 0;
}

static void
strip_ref_segment(const char *host, char *path)
{
	char *p, *end;

	if (!host_is(host, "amazon") || !(p = strstr(path, "/ref=")))
		return;
	end = strchr(p + 1, '/');
	memmove(p, end ? end : p + strlen(p), end ? strlen(end) + 1 : 1);
}

void
urlclean_strip(struct arena *a, struct url *u)
{
	char *host = hostname(a, u->netloc), *q = astrdup(a, u->query), *w = q;
	const char *p, *end;
	size_t n;

	for (p = u->query; *p; p = *end ? end + 1 : end) {
		end = p + strcspn(p, "&");
		n = (size_t)(end - p);
		if (!n || tracker(host, p, n))
			continue;
		if (w != q)
			*w++ = '&';
		memmove(w, p, n);
		w += n;
	}
	*w = 0;
	u->query = q;
	u->path = astrdup(a, u->path);
	strip_ref_segment(host, u->path);
}

static const char *
hop_target(struct arena *a, const struct url *u)
{
	char *host = hostname(a, u->netloc);
	const char *v;
	size_t i;

	for (i = 0; i < LEN(hops); i++)
		if (host_is(host, hops[i].host) && starts_with(u->path, hops[i].path) &&
		    web_url(v = query_param(a, u->query, hops[i].param)))
			return v;
	return NULL;
}

char *
urlclean_unwrap(struct arena *a, const char *url)
{
	struct url u;
	const char *next;
	int i;

	for (i = 0; i < MAX_HOPS && web_url(url); i++) {
		url_parse(a, url, &u);
		if (!(next = hop_target(a, &u)))
			break;
		url = next;
	}
	return astrdup(a, url);
}
