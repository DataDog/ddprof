INCLUDE = -Iinclude -I/home/sanchda/dev/binutils-gdb/include -I/home/sanchda/dev/binutils-gdb/bfd -I/home/sanchda/dev/binutils-gdb/binutils -I/home/sanchda/dev/libunwind/include
CFLAGS = -gdwarf -g3 -ggdb3 -std=c11 -O0 -D_GNU_SOURCE
TESTS := http perf pprof
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith
ANALYZER := -fanalyzer -fanalyzer-verbosity=2
ANALYZER :=
CC := gcc-10
CC := clang-11
LDFLAGS += -L/home/sanchda/dev/libunwind/src/.libs
LDLIBS := -lunwind -lprotobuf-c -lz -lpthread -lelf -lbfd -llzma
UNWDIR := /home/sanchda/dev/libunwind
UNWLIBS := $(UNWDIR)/src/.libs/libunwind-x86_64.a $(UNWDIR)/src/.libs/libunwind.a

# NOTE
# unwind-x86-64.a is a result of compiling libunwind, but it doesn't get installed
# by default.  You'll need to either copy it or do something more clever than I did
# during installation

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

.PHONY: all clean install

dd-prof: src/dd-prof.c src/profile.pb-c.c src/string_table.c src/dictionary.c src/pprof.c $(UNWLIBS)
	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)
#	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DD_UWDBG $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

ddog: src/ddog.c src/profile.pb-c.c src/string_table.c src/dictionary.c src/pprof.c
	$(CC) -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

http: src/eg_http.c src/append_string.c src/string_table.c src/profile.pb-c.c src/pprof.c
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE -DD_SANITY_CHECKS $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

appendstring: src/eg_appendstring.c src/append_string.c
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE -DD_SANITY_CHECKS $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

unwind: src/eg_unwind.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

perfunwind: src/eg_perfunwind.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS) 

perf: src/eg_perf.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

pprof: src/eg_pprof.c src/string_table.c src/pprof.c src/profile.pb-c.c
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

procutils: src/eg_procutils.c
	$(CC) $(CFLAGS) $(WARNS) $(ANALYZER) $(LDFLAGS) $(INCLUDE) -o eg/$@ $^ $(LDLIBS)

procutils-tidy: src/eg_procutils.c
	clang-tidy src/eg_procutils.c -header-filter=.* -checks=* -- -std=c11 $(INCLUDE)

collatz: src/demo_collatz.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

cstacks: src/demo_constantstacks.c $(UNWLIBS)
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
