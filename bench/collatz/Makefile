TARGETDIR ?= "../../release"
ifeq ($(origin CI_PIPELINE_ID), undefined)
	DDARGS := -DVER_REV=\"$(shell git rev-parse --short HEAD)\"
else
	DDARGS := -DVER_REV=\"$(CI_PIPELINE_ID)-$(shell git rev-parse --short HEAD)\"
endif

collatz: collatz.c
	mkdir -p $(TARGETDIR)
	$(CC) $(CFLAGS) $(DDARGS) -o $(TARGETDIR)/$@ $^ -lpthread

