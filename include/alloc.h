#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// TODO add counters and summon this through native profiler

// These are passed during initialization in /tmp/ddprof_allocprof
static void *(*libc_malloc)(size_t) = NULL;
static void *(*libc_realloc)(void *, size_t) = NULL;
static void *(*libc_calloc)(size_t, size_t) = NULL;
static void (*libc_free)(void *) = NULL;

// What could possibly go wrong?
__attribute__((constructor)) static void ddprof_allocprof_init() {
  int fd = open("/tmp/ddprof_allocprof", O_READ);
  read(fd, libc_malloc, 8);
  read(fd, libc_realloc, 8);
  read(fd, libc_calloc, 8);
  read(fd, libc_free, 8);
}

static void *malloc(size_t sz) { return libc_malloc(sz); }

static void *realloc(void *region, size_t sz) {
  return libc_realloc(region, sz);
}

static void *calloc(size_t cnt, size_t sz) { return libc_calloc(cnt, sz); }

static void free(void *region) { libc_free(region); }
