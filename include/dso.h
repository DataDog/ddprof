#ifndef _H_dso
#define _H_dso

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "ddprof/string_table.h"
#include "logger.h"
#include "signal_helper.h"

// TODO this isn't good
#define PID_MAX 4194304
// For compatibility with perf events
typedef struct DsoIn {
  uint64_t addr;
  uint64_t len;
  uint64_t pgoff;
  char *filename;
} DsoIn;

typedef struct Dso {
  uint64_t start;
  uint64_t end;
  uint64_t pgoff; // offset in _bytes_ of a page; not an offset in pages
  uint32_t filename;
  struct Dso *next;
} Dso;

typedef struct PidList {
  Dso *dsos[PID_MAX];
} PidList;

typedef struct DsoCache {
  uint64_t pgoff;
  uint32_t filename;
  uint32_t sz; // optimistic?
  void *region;
} DsoCache;

typedef enum DsoErr {
  DSO_OK = 0,
  DSO_EPID,
  DSO_EADDR,
  DSO_NOTFOUND,
} DsoErr;

void libdso_init();
char *dso_path(Dso *);
char *dsocache_path(DsoCache *);
void pid_free(int);
bool dso_overlap(Dso *, DsoIn *);
bool dso_isequal(Dso *, DsoIn *);
bool pid_add(int, DsoIn *);
FILE *procfs_map_open(int);
bool ip_in_procline(char *, uint64_t);
void pid_find_ip(int, uint64_t);
DsoIn *dso_from_procline(char *);
bool pid_backpopulate(int);
bool pid_fork(int, int);
Dso *dso_find(int, uint64_t);
DsoCache *dso_cache_add(Dso *);
DsoCache *dso_cache_find(Dso *);
bool pid_read_dso(int, void *, size_t, uint64_t);

#endif
