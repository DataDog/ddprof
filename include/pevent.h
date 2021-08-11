#pragma once

#include <sys/types.h>

#define MAX_NB_WATCHERS 200

typedef struct PEvent {
  int pos; // Index into the sample
  int fd;  // Underlying perf event FD
  struct perf_event_mmap_page *region;
  size_t reg_size; // size of region
} PEvent;

typedef struct PEventHdr {
  PEvent pes[MAX_NB_WATCHERS];
  size_t size;
  size_t max_size;
} PEventHdr;
