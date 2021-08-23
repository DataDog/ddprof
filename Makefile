##
## Warning : This makefile is only to build the dependencies. Look to CMake to build ddprof 
##

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
CFLAGS = -O2 -std=c11 -D_GNU_SOURCE -DMYNAME=\"collatz\"
WARNS := -Wall -Wextra -Wpedantic -Wno-missing-braces -Wno-missing-field-initializers -Wno-gnu-statement-expression -Wno-pointer-arith -Wno-gnu-folding-constant -Wno-zero-length-array
BUILDCHECK := 0  # Do we check the build with CLANG tooling afterward?
DDARGS :=
SANS :=

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

## Other parameters
# Directory structure and constants
TARGETDIR := $(abspath deliverables)

# Global aggregates
CWD := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
INCLUDE = -I$(CWD)/include
DIRS := $(TARGETDIR)

.PHONY: bench all
.DELETE_ON_ERROR:

# kinda phony
bench:
	@echo "Using $(CC)"
	@echo "Building bench with debug=$(DEBUG), analysis=$(ANALYSIS), safety=$(SAFETY), GNU_TOOLS=$(GNU_TOOLS)"
	@echo 
	@echo =============== BEGIN BUILD ===============
	$(MAKE) CC=$(strip $(CC)) CFLAGS="$(CFLAGS)" INCLUDE="$(INCLUDE)" DDPROF_DIR=$(CWD) TARGETDIR=$(strip $(TARGETDIR)) -C bench/collatz

all: bench
