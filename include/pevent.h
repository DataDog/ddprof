// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "perf_ringbuffer.h"

// Takes into account number of watchers * number of CPUs
#define MAX_NB_PERF_EVENT_OPEN 450

struct PerfEventAttributes;

typedef struct PEvent {
  int watcher_pos; // Index to the watcher
  int fd; // Underlying perf event FD for perf_events, otherwise an eventfd that
          // signals data is available in ring buffer
  int mapfd;               // FD for ring buffer, same as `fd` for perf events
  int attr_idx;            // matching perf_event_attr
  size_t ring_buffer_size; // size of the ring buffer
  bool custom_event; // true if custom event (not handled by perf, eg. memory
                     // allocations)
  RingBuffer rb;     // metadata and buffers for processing perf ringbuffer
} PEvent;

typedef struct PEventHdr {
  PEvent pes[MAX_NB_PERF_EVENT_OPEN];
  // Store the
  PerfEventAttributes *attrs;
  size_t size;
  size_t max_size;
} PEventHdr;

#ifdef __cplusplus
}
#endif
