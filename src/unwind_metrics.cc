// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_metrics.hpp"
#include "ddprof_stats.hpp"

static const DDPROF_STATS s_cycled_stats[] = {
    STATS_UNWIND_FRAMES,           STATS_UNWIND_ERRORS,
    STATS_UNWIND_TRUNCATED_INPUT,  STATS_UNWIND_TRUNCATED_OUTPUT,
    STATS_UNWIND_INCOMPLETE_STACK, STATS_UNWIND_AVG_STACK_SIZE};

void unwind_metrics_reset(void) {
  for (unsigned i = 0; i < std::size(s_cycled_stats); ++i)
    ddprof_stats_clear(s_cycled_stats[i]);
}
