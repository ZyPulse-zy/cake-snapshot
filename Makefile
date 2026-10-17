CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
WARN = -std=c11 -Wall -Wextra -Werror
PREFIX ?= /usr
DESTDIR ?=

.PHONY: all test sanitize integration install clean
all: build/cake-snapshot
build:
	mkdir -p $@
build/cake-snapshot: src/cake_snapshot.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(LDFLAGS) -o $@
build/test-decoder: tests/test_decoder.c src/cake_snapshot.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(LDFLAGS) -o $@
test: all build/test-decoder
	./build/test-decoder
	python3 tests/test_cli.py ./build/cake-snapshot
sanitize: | build
	$(CC) $(CPPFLAGS) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_decoder.c -o build/test-sanitize
	./build/test-sanitize
integration: all
	CAKE_TEST_PARENT_NETNS="$$(readlink /proc/self/ns/net)" unshare -Urn python3 tests/test_integration.py ./build/cake-snapshot
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 build/cake-snapshot $(DESTDIR)$(PREFIX)/bin/cake-snapshot
clean:
	rm -rf build

.PHONY: openwrt-package
openwrt-package: | build
	mkdir -p build/openwrt/cake-snapshot/src
	cp packaging/openwrt/Makefile COPYING build/openwrt/cake-snapshot/
	cp src/cake_snapshot.c build/openwrt/cake-snapshot/src/
