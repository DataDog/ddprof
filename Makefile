## Flag parameters
# DEBUG
#   0 - Release; don't include debugmode features like snapshotting pprofs,
#       checking assertions, etc
#   1 - Do those things
# SAFETY
#   0 - Don't add sanitizers
#   1 - Add address sanitization
#   2 - Add undefined sanitization
#   3 - Add both
# ANALYSIS
#   0 - Don't add static analysis passes
#   1 - -fanalyzer -fanalyzer-verbosity=2 (GCC only?; engage static for clang)
# GNU_TOOLS
#   0 - Use clang
#   1 - Use GCC and co
DEBUG ?= 1
SAFETY ?= 3
ANALYSIS ?= 1
GNU_TOOLS ?= 0

## Build parameters
CFLAGS = -O2 -std=c11 -D_GNU_SOURCE
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith -Wno-gnu-folding-constant -Wno-zero-length-array
BUILDCHECK := 0  # Do we check the build with CLANG tooling afterward?
DDARGS :=
SANS :=

## Mode overrides
ifeq ($(DEBUG),1)
	DDARGS += -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DDEBUG
	CFLAGS += -g
else
	DDARGS += -DVER_REV=\"release\"
	CFLAGS += -O2
endif

ifeq ($(SAFETY),1)
	SANS += -fsanitize=address
endif
ifeq ($(SAFETY),2)
	SANS += -fsanitize=undefined
endif
ifeq ($(SAFETY),3)
	SANS += -fsanitize=address,undefined
endif

# TODO this is probably not what you want
GNU_LATEST := $(shell /bin/bash -c 'command -v gcc{-11,-10,,-9,-8,-7} | head -n 1')
CLANG_LATEST := $(shell /bin/bash -c 'command -v clang{-12,-11,,-10,-9,-8} | head -n 1')
ifeq ($(GNU_TOOLS),1)
	CC := $(GNU_LATEST)
else
	CC := $(CLANG_LATEST)
endif
CC := $(if $(strip $(CC)), $(CC),clang)

ifeq ($(ANALYSIS),1)
	ifeq ($(GNU_TOOLS),1)
		CFLAGS += -fanalyzer -fanalyzer-verbosity=2
	else
		BUILDCHECK := 1
	endif
endif

## Other parameters
# Directory structure and constants
TARGETDIR := $(abspath release)
VENDIR := $(abspath vendor)
TMP := $(abspath tmp)

## Elfutils build parameters
# We can't use the repo elfutils because:
#  * Support for static libebpl backends
#  * Support for debuginfod
MD5_ELF := 6f58aa1b9af1a5681b1cbf63e0da2d67 # You need to generate this manually
VER_ELF := 0.183
TAR_ELF := elfutils-$(VER_ELF).tar.bz2
URL_ELF := https://sourceware.org/elfutils/ftp/$(VER_ELF)/$(TAR_ELF)
ELFUTILS = $(VENDIR)/elfutils
ELFLIBS := $(ELFUTILS)/libdwfl/libdwfl.a $(ELFUTILS)/libdw/libdw.a $(ELFUTILS)/libebl/libebl.a $(ELFUTILS)/libelf/libelf.a

## libddprof build parameters
LIBDDPROF := $(VENDIR)/libddprof

# Global aggregates
INCLUDE = -I$(LIBDDPROF)/src -I$(LIBDDPROF)/include -Iinclude -Iinclude/proto -I$(ELFUTILS) -I$(ELFUTILS)/libdw -I$(ELFUTILS)/libdwfl -I$(ELFUTILS)/libebl -I$(ELFUTILS)/libelf
LDLIBS := -l:libprotobuf-c.a -l:libbfd.a -lz -lpthread -llzma -ldl 
SRC := $(addprefix $(LIBDDPROF)/src/, string_table.c pprof.c http.c dd_send.c append_string.c) src/proto/profile.pb-c.c
DIRS := $(TARGETDIR) $(TMP)

.PHONY: build bench ddprof_banner format format-commit clean_deps publish all
.DELETE_ON_ERROR:

## Intermediate build targets (dependencies)
$(DIRS):
	mkdir -p $@

$(ELFLIBS): $(ELFUTILS)
	$(MAKE) -j4 -C $(ELFUTILS)

$(ELFUTILS):
	cd $(VENDIR) && curl -L --remote-name-all $(URL_ELF)
	echo $(MD5_ELF) $(VENDIR)/$(TAR_ELF) > $(VENDIR)/elfutils.md5
	md5sum --status -c $(VENDIR)/elfutils.md5
	mkdir -p $(ELFUTILS)
	tar --no-same-owner -C $(ELFUTILS) --strip-components 1 -xf $(VENDIR)/$(TAR_ELF)
	rm -rf $(VENDIR)/$(TAR_ELF)
	cd $(ELFUTILS) && ./configure CC=$(abspath $(GNU_LATEST)) --disable-debuginfod --disable-libdebuginfod --disable-symbol-versioning

$(LIBDDPROF):
	git submodule update --init

build: $(TARGETDIR)/ddprof


## Actual build targets
$(TARGETDIR)/ddprof: src/ddprof.c | $(TARGETDIR) $(ELFLIBS) $(LIBDDPROF) ddprof_banner
	$(CC) -Wno-macro-redefined $(DDARGS) $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o $@ $< $(SRC) $(ELFLIBS) $(LDLIBS)

# kinda phony
bench: 
	$(MAKE) CC=$(strip $(CC)) CFLAGS="$(CFLAGS)" TARGETDIR=$(strip $(TARGETDIR)) -C bench/collatz

help: $(TARGETDIR)/ddprof 
	tools/help_generate.sh

## Phony helper-targets
ddprof_banner:
	@echo "Using $(CC)"
	@echo "Building ddprof with debug=$(DEBUG), analysis=$(ANALYSIS), safety=$(SAFETY), GNU_TOOLS=$(GNU_TOOLS)"
	@echo "elfutils $(VER_ELF)"
	@git submodule
	@echo 
	@echo =============== BEGIN BUILD ===============

format:
	tools/clang_formatter.sh

format-commit:
	tools/clang_formatter.sh apply

clean_deps:
	rm -rf vendor/elfutils
	rm -rf tmp/*

clean: clean_deps
	rm $(TARGETDIR)/*

publish: ddprof
	$(eval BIN_NAME := $(shell $(TARGETDIR)/ddprof -v | sed 's/ /_/g'))
	$(eval TAR_NAME := $(BIN_NAME).tar.gz)
	tar -cf $(TARGETDIR)/$(TAR_NAME) $(TARGETDIR)/ddprof lib/x86_64-linux-gnu/elfutils/libebl_x86_64.so
	tools/upload.sh $(TAR_NAME)

all: ddprof bench
