BINUTILS = /home/sanchda/dev/binutils-gdb
LIBUNWIND = /home/sanchda/dev/libunwind
ELFUTILS = /home/sanchda/dev/elfutils
INCLUDE = -Iinclude -I$(BINUTILS)/include -I$(BINUTILS)/bfd -I$(BINUTILS)/binutils -I$(LIBUNWIND)/include -I$(ELFUTILS) -I$(ELFUTILS)/libdw
INCLUDE = -Iinclude -I$(ELFUTILS) -I$(ELFUTILS)/libdw -I$(ELFUTILS)/libdwfl -I$(ELFUTILS)/libebl -I$(ELFUTILS)/libelf
CFLAGS = -O0 -g -std=c11 -D_GNU_SOURCE -DDEBUG
TESTS := http perf pprof
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith -Wno-gnu-folding-constant
ANALYZER := -fanalyzer -fanalyzer-verbosity=2
ANALYZER :=
CC := gcc-10
CC := clang-11
LDFLAGS += -L/home/sanchda/dev/libunwind/src/.libs
LDLIBS := -lprotobuf-c -lz -lpthread -lelf -lbfd -llzma -ldl 
UNWLIBS := $(LIBUNWIND)/src/.libs/libunwind-x86_64.a $(LIBUNWIND)/src/.libs/libunwind.a
ELFLIBS := $(ELFUTILS)/libdwfl/libdwfl.a $(ELFUTILS)/libdw/libdw.a $(ELFUTILS)/libebl/libebl.a
SRC := src/string_table.c src/profile.pb-c.c src/pprof.c src/http.c src/dd_send.c src/append_string.c

# NOTE
# unwind-x86-64.a is a result of compiling libunwind, but it doesn't get installed
# by default.  You'll need to either copy it or do something more clever than I did
# during installation

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

.PHONY: all install

# -DD_UWDBG is useful here (prints unwinding diagnostics), just set the UNW_DEBUG_LEVEL environment variable
dd-prof: src/dd-prof.c $(SRC) $(ELFLIBS)
	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)
#	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DD_UNWDBG -fsanitize=address $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)
#	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -fsanitize=undefined $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)
#	$(CC) -Wno-macro-redefined -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DD_UWDBG $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

http: eg/http.c $(SRC)
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE -DD_SANITY_CHECKS -fsanitize=undefined $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

appendstring: eg/appendstring.c $(SRC)
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE -DD_SANITY_CHECKS $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

unwind: eg/unwind.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

perfunwind: eg/perfunwind.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS) 

perf: eg/perf.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

pprof: eg/pprof.c $(SRC)
	$(CC) -DKNOCKOUT_UNUSED -DD_LOGGING_ENABLE $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

procutils: eg/procutils.c
	$(CC) $(CFLAGS) $(WARNS) $(ANALYZER) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

procutils-tidy: src/procutils.c
	clang-tidy src/procutils.c -header-filter=.* -checks=* -- -std=c11 $(INCLUDE)

collatz: eg/collatz.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

cstacks: eg/cstacks.c $(UNWLIBS)
	$(CC) $(LIBDIRS) $(CFLAGS) $(WARNS) -Wno-macro-redefined $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS)

#strings: eg/strings.c src/string_table.c
#	$(CC) -DD_LOGGING_ENABLE $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

strings: eg/strings.c src/string_table.c
	$(CC) -DD_LOGGING_ENABLE -DD_SANITY_CHECKS $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

dictionary: eg/dict.c src/string_table.c src/dictionary.c
	$(CC) -DD_LOGGING_ENABLE $(WARNS) $(ANALYZER) $(CFLAGS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^

unwind2: eg/unwind2.c $(ELFLIBS)
	$(CC) -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DD_UNWDBG $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o bin/$@ $^ $(LDLIBS) -ldl

all: dd-prof http perf pprof collatz ddog

install: dd-prof 
	cp bin/dd-prof $(PREFIX)/bin/dd-prof

format:
	.build_tools/clang_formatter.sh --verbose -i --dry-run

format-commit:
	.build_tools/clang_formatter.sh --verbose -i

