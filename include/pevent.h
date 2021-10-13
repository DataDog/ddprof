#pragma once

#include <sys/types.h>

#include "perf_ringbuffer.h"

#define MAX_NB_WATCHERS 450

typedef struct PEvent {
  int pos;       // Index into the sample
  int fd;        // Underlying perf event FD
  RingBuffer rb; // metadata and buffers for processing perf ringbuffer
} PEvent;

typedef struct PEventHdr {
  PEvent pes[MAX_NB_WATCHERS];
  size_t size;
  size_t max_size;
} PEventHdr;
