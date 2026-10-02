CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local/lib/unity-stackcache

all: stackcache.so

stackcache.so: stackcache.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $< -ldl

test/bench: test/bench.c
	$(CC) $(CFLAGS) -o $@ $<

test: stackcache.so test/bench
	@echo "without stackcache:"; ./test/bench
	@echo "with stackcache:"; LD_PRELOAD=$(CURDIR)/stackcache.so ./test/bench

install: stackcache.so
	install -Dm644 stackcache.so $(PREFIX)/stackcache.so

clean:
	rm -f stackcache.so test/bench

.PHONY: all test install clean
