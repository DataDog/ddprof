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
  static constexpr unsigned k_data_fake_size = 8; // 8 to keep aligned struct
  perf_event_header hdr;
  struct sample_id sample_id;
  uint64_t addr; /* if PERF_SAMPLE_ADDR */
  uint64_t period;
  uint64_t abi;                   /* if PERF_SAMPLE_REGS_USER */
  uint64_t regs[PERF_REGS_COUNT]; /* if PERF_SAMPLE_REGS_USER */
  uint64_t size_stack;                  /* if PERF_SAMPLE_STACK_USER */
  std::byte data[k_data_fake_size]; /* requires PERF_SAMPLE_STACK_USER, dyn size will contain the actual size */
};
//  uint64_t dyn_size_stack;  /* added after the buffer */

static inline size_t SizeOfAllocationEvent(uint32_t stack_sample_user) {
  // stack_sample_user is 8 byte aligned
  // (Size of the event) + (stack size - 1) + sizeof(dyn_size field)
  return sizeof(AllocationEvent) + stack_sample_user
      - AllocationEvent::k_data_fake_size
      + sizeof(uint64_t);
}
} // namespace ddprof
