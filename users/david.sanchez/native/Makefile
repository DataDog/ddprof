INCLUDE = -Iinclude
CFLAGS = -ggdb3 -std=c11 -O0 -D_GNU_SOURCE
TESTS := http perf pprof
WARNS := -Wall -Wextra -Wpedantic -Werror #-Wfatal-errors
ANALYZER := -fanalyzer
CC := gcc-10
LDLIBS := -lunwind -lprotobuf-c -lz -lpthread

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

.PHONY: all clean install

rambutan: src/rambutan.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

ddog: src/ddog.c src/profile.pb-c.c
	$(CC) -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

http: src/eg_http.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

perf: src/eg_perf.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

pprof: src/eg_pprof.c src/profile.pb-c.c
	$(CC) -DKNOCKOUT_UNUSED $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

procutils: src/eg_procutils.c
	$(CC) $(CFLAGS) $(WARNS) $(ANALYZER) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

procutils-tidy: src/eg_procutils.c
	clang-tidy src/eg_procutils.c -header-filter=.* -checks=* -- -std=c11 $(INCLUDE)

collatz: src/demo_collatz.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

all: rambutan http perf pprof collatz ddog

clean:
	-rm -rf bin/* eg/*

install: rambutan
	cp bin/rambutan $(PREFIX)/bin/rambutan
