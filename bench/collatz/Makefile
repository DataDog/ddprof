TARGETDIR ?= "../../release"

collatz: collatz.c
	$(CC) $(CFLAGS) -o $(TARGETDIR)/$@ $^ -lpthread

sleeptest: %.c
	$(CC) - -O
