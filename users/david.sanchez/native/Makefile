BINUTILS = /home/sanchda/dev/binutils-gdb
INCLUDE = -Iinclude -I$(BINUTILS)/include -I$(BINUTILS)/bfd -I$(BINUTILS)/binutils -I/home/sanchda/dev/libunwind/include
CFLAGS = -O0 -g -std=c11 -D_GNU_SOURCE
TESTS := http perf pprof
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith -Wno-gnu-folding-constant
ANALYZER := -fanalyzer -fanalyzer-verbosity=2
ANALYZER :=
CC := gcc-10
CC := clang-11
LDFLAGS += -L/home/sanchda/dev/libunwind/src/.libs
LDLIBS := -lunwind -lprotobuf-c -lz -lpthread -lelf -lbfd -llzma
UNWDIR := /home/sanchda/dev/libunwind
UNWLIBS := $(UNWDIR)/src/.libs/libunwind-x86_64.a $(UNWDIR)/src/.libs/libunwind.a
SRC := src/string_table.c src/profile.pb-c.c src/pprof.c src/http.c src/dd_send.c src/append_string.c

# NOTE
# unwind-x86-64.a is a result of compiling libunwind, but it doesn't get installed
# by default.  You'll need to either copy it or do something more clever than I did
# during installation

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

.PHONY: all clean install

# -DD_UWDBG is useful here, just set the UNW_DEBUG_LEVEL environment variable
dd-prof: src/dd-prof.c $(SRC) $(UNWLIBS)
	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DD_UNWDBG -fsanitize=address $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)
#	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -fsanitize=undefined $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)
#	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DD_UWDBG $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

http: eg/eg_http.c $(SRC)
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE -DD_SANITY_CHECKS -fsanitize=undefined $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

appendstring: eg/eg_appendstring.c $(SRC)
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE -DD_SANITY_CHECKS $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

unwind: eg/eg_unwind.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

perfunwind: eg/eg_perfunwind.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS) 

perf: eg/eg_perf.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

pprof: eg/eg_pprof.c $(SRC)
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

procutils: eg/eg_procutils.c
	$(CC) $(CFLAGS) $(WARNS) $(ANALYZER) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

procutils-tidy: src/eg_procutils.c
	clang-tidy src/eg_procutils.c -header-filter=.* -checks=* -- -std=c11 $(INCLUDE)

collatz: src/demo_collatz.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

cstacks: eg/demo_constantstacks.c $(UNWLIBS)
	$(CC) $(LIBDIRS) $(CFLAGS) $(WARNS) -Wno-macro-redefined $(LDFLAGS) $(INCLUDE) -o bin/$@.remote $^ $(LDLIBS)
	$(CC) $(LIBDIRS) $(CFLAGS) $(WARNS) -DD_LOCAL -Wno-macro-redefined $(LDFLAGS) $(INCLUDE) -o bin/$@.local $^ $(LDLIBS)

#strings: src/demo_strings.c src/string_table.c
#	$(CC) -DD_LOGGING_ENABLE $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

strings: src/demo_strings.c src/string_table.c
	$(CC) -DD_LOGGING_ENABLE -DD_SANITY_CHECKS $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

dictionary: src/demo_dict.c src/string_table.c src/dictionary.c
	$(CC) -DD_LOGGING_ENABLE $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

all: dd-prof http perf pprof collatz ddog

clean:
	-rm -rf bin/* eg/*

install: dd-prof 
	cp bin/dd-prof $(PREFIX)/bin/dd-prof

format:
	.build_tools/clang_formatter.sh --verbose -i --dry-run

format-commit:
	.build_tools/clang_formatter.sh --verbose -i
