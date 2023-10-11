// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_metrics.hpp"
#include "ddprof_stats.hpp"

namespace ddprof {
namespace {
constexpr DDPROF_STATS s_cycled_stats[] = {
    STATS_UNWIND_FRAMES,           STATS_UNWIND_ERRORS,
    STATS_UNWIND_TRUNCATED_INPUT,  STATS_UNWIND_TRUNCATED_OUTPUT,
    STATS_UNWIND_INCOMPLETE_STACK, STATS_UNWIND_AVG_STACK_SIZE,
    STATS_UNWIND_AVG_STACK_DEPTH};
}

void unwind_metrics_reset() {
  for (auto s_cycled_stat : s_cycled_stats) {
    ddprof_stats_clear(s_cycled_stat);
  }
}
} // namespace ddprof
