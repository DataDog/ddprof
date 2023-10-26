// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.

#pragma once

#include <linux/perf_event.h>

namespace ddprof {

// AllocationEvent
// This represent a sampled allocation.
// We Keep the same layout as a perf event to unify the code paths.
struct AllocationEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
  uint64_t addr; /* if PERF_SAMPLE_ADDR */
  uint64_t period;
  uint64_t abi;                         /* if PERF_SAMPLE_REGS_USER */
  uint64_t regs[k_perf_register_count]; /* if PERF_SAMPLE_REGS_USER */
  uint64_t size_stack;                  /* if PERF_SAMPLE_STACK_USER */
  std::byte data[]; /* requires PERF_SAMPLE_STACK_USER, dyn size
                        will contain the actual size */
};
// An extra field is added after the end to communicate the dyn_size
//  uint64_t dyn_size_stack;

inline size_t sizeof_allocation_event(uint32_t stack_size) {
  // (Size of the event) + stack_size + sizeof(dyn_size field)
  return sizeof(AllocationEvent) + stack_size + sizeof(uint64_t);
}
} // namespace ddprof
