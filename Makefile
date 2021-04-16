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
CC := clang-11
CFLAGS = -O2 -std=c11 -D_GNU_SOURCE
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith -Wno-gnu-folding-constant
BUILDCHECK := 0  # Do we check the build with CLANG tooling afterward?
DDARGS :=
SANS :=

## Mode overrides
ifeq ($(DEBUG),1)
	DDARGS += -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDD_DBG_PRINTARGS -DDEBUG
	CFLAGS += -g
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

## Elfutils build parameters
# We can't use the repo elfutils because:
#  * Support for static libebpl backends
#  * Support for debuginfod
MD5_ELF := 6f58aa1b9af1a5681b1cbf63e0da2d67 # You need to generate this manually
VER_ELF := 0.183
TAR_ELF := elfutils-$(VER_ELF).tar.bz2
URL_ELF := https://sourceware.org/elfutils/ftp/$(VER_ELF)/$(TAR_ELF)
ELFUTILS = $(VENDIR)/elfutils
ELFLIBS := $(ELFUTILS)/libdwfl/libdwfl.a $(ELFUTILS)/libdw/libdw.a $(ELFUTILS)/libebl/libebl.a

## libddprof build parameters
LIBDDPROF := $(VENDIR)/libddprof

# Global aggregates
INCLUDE = -I $(LIBDDPROF)/src -I$(LIBDDPROF)/include -Iinclude -Iinclude/proto -I$(ELFUTILS) -I$(ELFUTILS)/libdw -I$(ELFUTILS)/libdwfl -I$(ELFUTILS)/libebl -I$(ELFUTILS)/libelf
LDLIBS := -l:libprotobuf-c.a -l:libelf.a -l:libbfd.a -lz -lpthread -llzma -ldl 
SRC := $(addprefix $(LIBDDPROF)/src/, string_table.c pprof.c http.c dd_send.c append_string.c) src/proto/profile.pb-c.c

# If we're here, then we've done all the optional processing stuff, so export
# some variables to sub-makes
export CC
export CFLAGS
export TARGETDIR

.PHONY: bench ddprof_banner format format-commit clean_deps publish all

## Intermediate build targets (dependencies)
$(TARGETDIR):
	mkdir $@

$(VENDIR)/elfutils:
	cd $(VENDIR) && \
		mkdir elfutils && \
		curl -L --remote-name-all $(URL_ELF) && \
		echo $(MD5_ELF)  $(TAR_ELF) > elfutils.md5 && \
		md5sum --status -c elfutils.md5 && \
		tar --no-same-owner -C elfutils --strip-components 1 -xf $(TAR_ELF) && \
		rm -rf $(TAR_ELF) && \
		cd elfutils && \
		./configure --disable-debuginfod --disable-libdebuginfod && \
		make

## Actual build targets
ddprof: src/ddprof.c | ddprof_banner $(TARGETDIR) $(VENDIR)/elfutils
	$(CC) -Wno-macro-redefined $(DDARGS) $(LIBDIRS) $(CFLAGS) $(WARNS) $(LDFLAGS) $(INCLUDE) -o $(TARGETDIR)/$@ $(SRC) $^ $(ELFLIBS) $(LDLIBS)

# kinda phony
bench: 
	$(MAKE) -C bench/collatz

## Phony helper-targets
ddprof_banner:
	@echo "Using $(CC)"
	@echo "Building ddprof with debug=$(DEBUG), analysis=$(ANALYSIS), safety=$(SAFETY)"
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

publish: ddprof
	$(eval BIN_NAME := $(shell $(TARGETDIR)/ddprof -v | sed 's/ /_/g'))
	$(eval TAR_NAME := $(BIN_NAME).tar.gz)
	tar -cf $(TARGETDIR)/$(TAR_NAME) $(TARGETDIR)/ddprof lib/x86_64-linux-gnu/elfutils/libebl_x86_64.so
	tools/upload.sh $(TAR_NAME)

all: ddprof bench
