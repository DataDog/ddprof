CC := clang-10
CFLAGS := -g -O2

collatz: collatz.c
	$(CC) $(CFLAGS) -o ../../build/$@ $^ -lpthread

sleeptest: %.c
	$(CC) - -O
