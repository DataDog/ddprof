## Flag parameters
# DEBUG
#   0 - Release; don't include debugmode features like snapshotting pprofs,
#       checking assertions, etc
#   1 - Do those things
# SAFETY
#   0 - Don't add sanitizers
#   1 - Add address
#   2 - Add undefined
#   3 - A+U
#   4 - Add thread
#   5 - ILLEGAL (can't do A+T)
#   6 - U+T
#   7 - ILLEGAL (can't do A+T)
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
CFLAGS = -O2 -std=c11 -D_GNU_SOURCE -DMYNAME=\"ddprof\"
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith -Wno-gnu-folding-constant -Wno-zero-length-array
BUILDCHECK := 0  # Do we check the build with CLANG tooling afterward?
DDARGS :=
SANS :=

# Because ddprof uses C11, which is *not* a subset of any C++ standard, it is
# necessary to link against the C++ runtime from C.  In GCC this is easy, but
# in clang this is too hard for David to figure out.  Since we presume at this
# time that both the GNU and llvm C/C++ build environments are available, we
# find the latest instance of the static GCC libstdc++ library
LIBSTDCXX := $(shell tools/find_libstdcxx.sh)

## Mode overrides
ifeq ($(DEBUG),1)
  DDARGS += -DKNOCKOUT_UNUSED -DDD_DBG_PROFGEN -DDEBUG
  CFLAGS += -g
  CFLAGS += -rdynamic
  CFLAGS += -O0
  CFLAGS := $(filter-out -O2, $(CFLAGS))
else
  CFLAGS += -O2
  CFLAGS += -g
  DDARGS += -DNDEBUG
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
ifeq ($(SAFETY),4)
  SANS += -fsanitize=thread
endif
ifeq ($(SAFETY),6)
  SANS += -fsanitize=undefined,thread
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

# If this is happening in CI, name accordingly
VERNAME :=
ifeq ($(origin CI_PIPELINE_ID), undefined)
  VERNAME :=$(shell git rev-parse --short HEAD 2> /dev/null || echo "local"| xargs)
else
  VERNAME :=$(CI_PIPELINE_ID)-$(shell git rev-parse --short HEAD)
endif

ifeq ($(DEBUG), 1)
  VERNAME := $(VERNAME)debug
endif
DDARGS += -DVER_REV=\"$(VERNAME)\"

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
ELFLIBS := $(ELFUTILS)/libdw/libdw.a $(ELFUTILS)/libelf/libelf.a

## https://gitlab.ddbuild.io/DataDog/libddprof/-/jobs/72495950
VER_LIBDDPROF := 79bbf4a2 #Short commit number from CI (used in export job of libddprof)
SHA256_LIBDDPROF := dbd67207af1bd090ad34a30b15afe7da3ec3c7ed1c56762a5f7764565ef8f1b8 # You need to generate this manually

LIBDDPROF := $(VENDIR)/libddprof
LIBDDPROF_LIB := $(LIBDDPROF)/RelWithDebInfo/lib64/libddprof.a

LIBLLVM := $(VENDIR)/llvm/include
LIBLLVM_SRC := $(VENDIR)/llvm/lib

# Global aggregates
CWD := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SOURCE_DIR := $(CWD)
INCLUDE = -I$(LIBDDPROF)/RelWithDebInfo/include -I$(CWD)/include -Iinclude/proto -I$(ELFUTILS) -I$(ELFUTILS)/libdw -I$(ELFUTILS)/libdwfl -I$(ELFUTILS)/libebl -I$(ELFUTILS)/libelf
LDLIBS := -l:libprotobuf-c.a -l:libbfd.a -l:libz.a -lpthread -l:liblzma.a -ldl $(LIBSTDCXX)
SRC := $(CWD)/src/proto/profile.pb-c.c $(CWD)/src/ddprofcmdline.c $(CWD)/src/ipc.c $(CWD)/src/logger.c $(CWD)/src/signal_helper.c $(CWD)/src/version.c $(CWD)/src/statsd.c
DIRS := $(TARGETDIR) $(TMP)

.PHONY: build deps elfutils demangle bench ddprof_banner format format-commit clean_deps publish all
.DELETE_ON_ERROR:

## Intermediate build targets (dependencies)
$(TMP):
	mkdir -p $@

$(TARGETDIR):
	mkdir -p $@

$(ELFLIBS): $(ELFUTILS)
	cd $(ELFUTILS) && ./configure CC=$(abspath $(GNU_LATEST)) --disable-debuginfod --disable-libdebuginfod --disable-symbol-versioning
	$(MAKE) -j4 -C $(ELFUTILS)

$(ELFUTILS):
	mkdir -p $(VENDIR)
	ls -l $(VENDIR)
	cd $(VENDIR) && curl -L --remote-name-all $(URL_ELF)
	echo $(MD5_ELF) $(VENDIR)/$(TAR_ELF) > $(VENDIR)/elfutils.md5
	md5sum --status -c $(VENDIR)/elfutils.md5
	mkdir -p $(ELFUTILS)
	ls -l $(ELFUTILS)
	tar --no-same-owner -C $(ELFUTILS) --strip-components 1 -xf $(VENDIR)/$(TAR_ELF)
	rm -rf $(VENDIR)/$(TAR_ELF)

$(LIBDDPROF):
	./tools/fetch_libddprof.sh ${VER_LIBDDPROF} ${SHA256_LIBDDPROF} $(VENDIR)

LLVM_TMP := $(TMP)/demangle
_LLVM_OBJS := RustDemangle.o ItaniumDemangle.o MicrosoftDemangleNodes.o MicrosoftDemangle.o Demangle.o llvmdemangle.o
LLVM_OBJS := $(patsubst %,$(LLVM_TMP)/%,$(_LLVM_OBJS))

$(LIBLLVM):
	./tools/fetch_llvm_demangler.sh 

$(LLVM_TMP):
	mkdir -p $(LLVM_TMP)

$(LLVM_TMP)/%.o: $(LIBLLVM_SRC)/Demangle/%.cpp $(LIBLLVM) | $(LLVM_TMP)
	$(CXX) $(WARNS) -Wno-unused-parameter $(INCLUDE) -I$(LIBLLVM) -c -o $@ $<

$(LLVM_TMP)/llvmdemangle.o: $(LIBLLVM) | $(LLVM_TMP)
	$(CXX) $(WARNS) $(INCLUDE) -I$(LIBLLVM) -c -o $@ src/demangle.cpp

$(TMP)/demangle.a: $(LLVM_OBJS)
	ar rvcs $@ $^

demangle: $(TMP)/demangle.a

ddprof: $(TARGETDIR)/ddprof
build: |ddprof help
deps: $(LIBDDPROF) $(ELFLIBS) $(LIBLLVM)

# HACK: For some reason HTTP doesn't work in David's local containers, but
# pulling down dependencies outside and then compiling works
pull: $(LIBDDPROF) $(ELFUTILS) $(LIBLLVM)

## Actual build targets
$(TARGETDIR)/ddprof: src/ddprof.c $(TMP)/demangle.a| $(TARGETDIR) $(ELFLIBS) $(LIBDDPROF) ddprof_banner $(LIBDDPROF_LIB)
	$(CC) -Wno-macro-redefined $(DDARGS) $(LIBDIRS) $(CFLAGS) -static-libgcc $(WARNS) $(SANS) $(LDFLAGS) $(INCLUDE) -o $@ $< $(SRC) $(TMP)/demangle.a $(ELFLIBS) $(LDLIBS) $(LIBDDPROF_LIB)

logger: src/eg/logger.c src/logger.c
	$(CC) $(CFLAGS) $(WARNS) $(SANS) -DPID_OVERRIDE -Iinclude -o $(TARGETDIR)/$@ $^

# kinda phony
bench: 
	$(MAKE) CC=$(strip $(CC)) CFLAGS="$(CFLAGS)" INCLUDE="$(INCLUDE)" DDPROF_DIR=$(CWD) TARGETDIR=$(strip $(TARGETDIR)) -C bench/collatz

help: $(TARGETDIR)/ddprof 
	tools/help_generate.sh

## Phony helper-targets
ddprof_banner:
	@echo "Using $(CC)"
	@echo "Building ddprof with debug=$(DEBUG), analysis=$(ANALYSIS), safety=$(SAFETY), GNU_TOOLS=$(GNU_TOOLS)"
	@echo "elfutils $(VER_ELF)"
	@echo 
	@echo =============== BEGIN BUILD ===============

format:
	tools/style-check.sh

format-commit:
	tools/style-check.sh apply

clean_deps:
	rm -rf vendor/elfutils
	rm -rf vendor/libddprof*
	rm -rf $(TMP)/*

clean: clean_deps
	rm $(TARGETDIR)/*

publish: ddprof
	$(eval BIN_NAME := $(shell $(TARGETDIR)/ddprof -v | sed 's/ /_/g'))
	$(eval TAR_NAME := $(BIN_NAME).tar.gz)
	tar -cf $(TARGETDIR)/$(TAR_NAME) $(TARGETDIR)/ddprof lib/x86_64-linux-gnu/elfutils/libebl_x86_64.so
	tools/upload.sh $(TAR_NAME)

all: ddprof bench
