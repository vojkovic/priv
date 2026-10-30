CURL_IMPERSONATE_VERSION = v2.3.2
CURL_IMPERSONATE = deps/curl-impersonate
GIT_HEAD := $(shell git rev-parse HEAD 2>/dev/null)
ifeq ($(VERSION),)
VERSION := $(if $(GIT_HEAD),$(shell git log -1 --format=%cd+%h --date=format:%Y.%m.%d --abbrev=9)$(shell git diff --quiet --diff-filter=d HEAD -- || echo -dirty))
endif

LIBC := $(shell ldd --version 2>&1 | grep -qi musl && echo musl || echo gnu)
TRIPLE = $(shell uname -m)-linux-$(LIBC)

PKG_CONFIG ?= pkg-config
PKG_FLAGS = $(if $(STATIC),--static)
XML_CFLAGS := $(shell $(PKG_CONFIG) --cflags libxml-2.0)
XML_LIBS := $(shell $(PKG_CONFIG) $(PKG_FLAGS) --libs libxml-2.0)
CURL_LIBS = $(CURL_IMPERSONATE)/libcurl-impersonate.a \
	$(shell sed -n 's/^Libs.private: //p' $(CURL_IMPERSONATE)/libcurl-impersonate.pc 2>/dev/null)

WARNINGS = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
	-Wno-unused-parameter -Wno-overlength-strings

CFLAGS ?= -O2
override CPPFLAGS += -D_GNU_SOURCE -DCURL_STATICLIB -I$(CURL_IMPERSONATE)/include -Iobj -Isrc -I.
override CFLAGS += -std=c11 $(WARNINGS) $(XML_CFLAGS)
override LDFLAGS += $(if $(STATIC),-static)
override LDLIBS += $(CURL_LIBS) $(XML_LIBS) -lm

SRC = $(wildcard src/*.c src/*/*.c)
OBJ = $(SRC:%.c=obj/%.o) obj/i18n.o obj/lib/yyjson/yyjson.o obj/lib/picohttpparser/picohttpparser.o
GENERATED = src/search/locales.c
FORMAT_SRC = $(filter-out $(GENERATED),$(wildcard src/*.c src/*/*.c src/*/*.h tools/*.c))
LINT_SRC = $(filter %.c,$(FORMAT_SRC))

all: priv

priv: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

obj/tools/embed: tools/embed.c
	@mkdir -p $(@D)
	$(CC) -o $@ $<

obj/style.h: static/style.css obj/tools/embed
	obj/tools/embed style_css static/style.css > $@

obj/script.h: static/priv.js obj/tools/embed
	obj/tools/embed priv_js static/priv.js > $@

obj/calc.h: static/calc.js obj/tools/embed
	obj/tools/embed calc_js static/calc.js > $@

obj/color.h: static/color.js obj/tools/embed
	obj/tools/embed color_js static/color.js > $@

obj/favicon_svg.h: static/favicon.svg obj/tools/embed
	obj/tools/embed favicon_svg static/favicon.svg > $@

obj/version.h: FORCE
	@mkdir -p $(@D)
	@printf '#define VERSION "%s"\n' '$(VERSION)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

obj/src/routes.o: obj/style.h obj/script.h obj/calc.h obj/color.h obj/favicon_svg.h
$(filter obj/src/render/%,$(OBJ)): obj/version.h

obj/engines.ok: $(wildcard src/engines/*.c) src/engines/engines.def
	@mkdir -p $(@D)
	@grep -ho '^struct engine engine_[a-z0-9_]*' src/engines/*.c | sed 's/.*engine_//' | sort > $@.defined
	@sed -n 's/^ENGINE(\(.*\))/\1/p' src/engines/engines.def | sort > $@.listed
	@diff $@.defined $@.listed >/dev/null || { echo "src/engines/engines.def and the engines in src/engines/*.c differ:"; diff $@.defined $@.listed; exit 1; }
	@touch $@

obj/src/engines/engines.o: obj/engines.ok

obj/tools/po2c: tools/po2c.c
	@mkdir -p $(@D)
	$(CC) -o $@ $<

obj/i18n.c: $(wildcard translations/*.po) obj/tools/po2c
	obj/tools/po2c translations/*.po > $@

obj/i18n.o: obj/i18n.c src/core/i18n.h
	$(CC) $(CPPFLAGS) -O2 -c -o $@ $<

obj/lib/yyjson/yyjson.o: lib/yyjson/yyjson.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -O2 -c -o $@ $<

obj/lib/picohttpparser/picohttpparser.o: lib/picohttpparser/picohttpparser.c lib/picohttpparser/picohttpparser.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -O2 -c -o $@ $<

obj/src/%.o: src/%.c $(wildcard src/*/*.h) $(CURL_IMPERSONATE)/libcurl-impersonate.a
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(CURL_IMPERSONATE)/libcurl-impersonate.a:
	@mkdir -p $(CURL_IMPERSONATE)
	curl -fsSL https://github.com/lexiforest/curl-impersonate/releases/download/$(CURL_IMPERSONATE_VERSION)/libcurl-impersonate-$(CURL_IMPERSONATE_VERSION).$(TRIPLE).tar.gz \
		| tar -xz -C $(CURL_IMPERSONATE)

format:
	clang-format -i $(FORMAT_SRC)

format-check:
	clang-format --dry-run -Werror $(FORMAT_SRC)

lint:
	@$(MAKE) -s --no-print-directory -j$(shell nproc) lint-files

lint-files: $(LINT_SRC:%.c=obj/lint/%.ok)

obj/lint/%.ok: %.c $(wildcard src/*/*.h) .clang-tidy | obj/style.h obj/script.h obj/calc.h obj/color.h obj/favicon_svg.h obj/version.h $(CURL_IMPERSONATE)/libcurl-impersonate.a
	@mkdir -p $(@D)
	@out=$$(clang-tidy --quiet $< -- $(CPPFLAGS) $(CFLAGS) -DCURL_DISABLE_TYPECHECK 2>&1) || \
		{ echo "$$out" | grep -v 'warnings* generated\.$$'; exit 1; }
	@cppcheck --quiet --error-exitcode=1 --std=c11 --enable=warning,performance,portability \
		--check-level=exhaustive --inline-suppr --suppress=missingIncludeSystem --suppress='*:lib/*' \
		-D_GNU_SOURCE -DYYJSON_HAS_STDINT_H=1 -Isrc -I. -Iobj $<
	@touch $@

docker:
	docker build -t priv .

clean:
	rm -rf obj priv

FORCE:

.PHONY: all clean FORCE format format-check lint lint-files docker
