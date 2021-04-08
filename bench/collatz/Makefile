CC := clang-10
CFLAGS := -g -O1

collatz: collatz.c
	$(CC) $(CFLAGS) -o ../../build/$@ $^ -lpthread

sleeptest: %.c
	$(CC) - -O
