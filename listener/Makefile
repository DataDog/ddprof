CC=gcc-10
CFLAGS=-std=c11 -D_GNU_SOURCE
INCLUDES="-Iinclude/"

listener: src/listener.c
	$(CC) $(INCLUDES) $(CFLAGS) -o bin/$@ $^
