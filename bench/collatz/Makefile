# TODO to be unified with switch to different build management system
GNU_LATEST := $(shell /bin/bash -c 'command -v gcc{-11,-10,,-9,-8,-7} | head -n 1')
CLANG_LATEST := $(shell /bin/bash -c 'command -v clang{-12,-11,,-10,-9,-8} | head -n 1')
CFLAGS := $(filter-out -O2, $(CFLAGS))
CFLAGS := $(filter-out -O1, $(CFLAGS))
ifeq ($(GNU_TOOLS),1)
  CC := $(GNU_LATEST)
else
  CC := $(CLANG_LATEST)
endif
CC := $(if $(strip $(CC)), $(CC),clang)

TARGETDIR ?= "../../release"
ifeq ($(origin CI_PIPELINE_ID), undefined)
	DDARGS := -DVER_REV=\"$(shell git rev-parse --short HEAD)\"
else
	DDARGS := -DVER_REV=\"$(CI_PIPELINE_ID)-$(shell git rev-parse --short HEAD)\"
endif

collatz: collatz.c
	mkdir -p $(TARGETDIR)
	$(CC) $(CFLAGS) $(DDARGS) $(INCLUDE) -o $(TARGETDIR)/$@ $^ $(DDPROF_DIR)/src/statsd.c -lpthread
