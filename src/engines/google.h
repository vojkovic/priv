#ifndef GOOGLE_H
#define GOOGLE_H

/*
 * Google gives the plain, no javascript page (real links, no redirects to
 * enable js) to any user agent when the url ends with its ad preview flags.
 * adtest-useragent says which device to pretend to be, and "preview" has to
 * come last or you get the enable-javascript page.
 *
 * web, news and videos use the samsung phone, images need the opera string
 * (it is only a value in the url, the request's own user agent can be anything)
 *
 * the images string has % escapes in it, so never use these as a printf format
 */
#define GOOGLE_ADTEST "&adtest=on&adtest-useragent=SAMSUNG-GT-S5230&adtest=preview"
#define GOOGLE_ADTEST_IMAGES \
	"&adtest=on&adtest-useragent=Mozilla%2F4.0%20%28compatible%3B%20MSIE%206.0%3B%20Symbian%20OS%3B%20" \
	"SAMSUNG-GT-S5230%29%20Opera%208.65&adtest=preview"

#endif
