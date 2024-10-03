// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <linux/perf_event.h>
#include <type_traits>

// Extend the perf event types
// There are <30 different perf events (starting at 1000 seems safe)
enum : uint16_t {
  PERF_CUSTOM_EVENT_DEALLOCATION = 1000,
  PERF_CUSTOM_EVENT_CLEAR_LIVE_ALLOCATION
};

static_assert(static_cast<uint32_t>(PERF_CUSTOM_EVENT_DEALLOCATION) >
                  PERF_RECORD_MAX,
              "Error from PERF_CUSTOM_EVENT_DEALLOCATION definition");

namespace ddprof {

// Custom sample type
struct DeallocationEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
  uintptr_t ptr;
};

// Event to notify we have tracked too many allocations
struct ClearLiveAllocationEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
};

} // namespace ddprof
