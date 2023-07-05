// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.

#pragma once

#include <linux/perf_event.h>

namespace ddprof {

struct AllocationEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
  uint64_t addr; /* if PERF_SAMPLE_ADDR */
  uint64_t period;
  uint64_t abi;                   /* if PERF_SAMPLE_REGS_USER */
  uint64_t regs[PERF_REGS_COUNT]; /* if PERF_SAMPLE_REGS_USER */
  uint64_t size;                  /* if PERF_SAMPLE_STACK_USER */
  std::byte data[1]; /* if PERF_SAMPLE_STACK_USER, the actual size will be
                                     determined at runtime */
  uint64_t dyn_size; /* if PERF_SAMPLE_STACK_USER && size != 0 */
};
} // namespace ddprof
