// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "perf_ringbuffer.hpp"

#include <sys/types.h>

namespace ddprof {

// Takes into account number of watchers * number of CPUs
inline constexpr size_t k_max_nb_perf_event_open{450};

struct PEvent {
  int watcher_pos; // Index to the watcher (containing perf event config)
  int fd; // Underlying perf event FD for perf_events, otherwise an eventfd that
          // signals data is available in ring buffer
  int mapfd;               // FD for ring buffer, same as `fd` for perf events
  int attr_idx;            // matching perf_event_attr
  size_t ring_buffer_size; // size of the ring buffer
  RingBufferType ring_buffer_type;
  bool custom_event; // true if custom event (not handled by perf, eg. memory
                     // allocations)
  RingBuffer rb;     // metadata and buffers for processing perf ringbuffer
  std::vector<int>
      sub_fds; // perf FDs of other events outputting to the same ring buffer
               // (eg. perf events for other process threads in PID mode)
};

struct PEventHdr {
  PEvent pes[k_max_nb_perf_event_open];
  // Attributes of successful perf event opens
  size_t size;
  size_t max_size;
  perf_event_attr attrs[kMaxTypeWatcher];
  size_t nb_attrs;
};

} // namespace ddprof
