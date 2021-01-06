INCLUDE = -Iinclude -I/home/sanchda/dev/binutils-gdb/include -I/home/sanchda/dev/binutils-gdb/bfd -I/home/sanchda/dev/binutils-gdb/binutils
CFLAGS = -ggdb3 -std=c11 -O0 -D_GNU_SOURCE
TESTS := http perf pprof
WARNS := -Wall -Wextra -Wpedantic -Werror #-Wfatal-errors
ANALYZER := -fanalyzer
CC := gcc-10
LDLIBS := -lunwind -lprotobuf-c -lz -lpthread -lunwind -lunwind-x86_64 -lelf -lbfd

# NOTE
# unwind-x86-64.a is a result of compiling libunwind, but it doesn't get installed
# by default.  You'll need to either copy it or do something more clever than I did
# during installation

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

.PHONY: all clean install

dd-prof: src/dd-prof.c src/profile.pb-c.c
	$(CC) -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

ddog: src/ddog.c src/profile.pb-c.c
	$(CC) -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

http: src/eg_http.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

unwind: src/eg_unwind.c
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

strings: src/demo_strings.c
	$(CC) -DD_ALLOC_DBG -DD_LOGGING_ENABLE $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

all: rambutan http perf pprof collatz ddog

clean:
	-rm -rf bin/* eg/*

install: rambutan
	cp bin/rambutan $(PREFIX)/bin/rambutan
