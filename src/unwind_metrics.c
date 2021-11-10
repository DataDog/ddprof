// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_metrics.h"

static const DDPROF_STATS s_cycled_stats[] = {STATS_UNWIND_FRAMES,
                                              STATS_UNWIND_ERRORS};

#define cycled_stats_sz (sizeof(s_cycled_stats) / sizeof(DDPROF_STATS))
void unwind_metrics_reset(void) {
  for (unsigned i = 0; i < cycled_stats_sz; ++i)
    ddprof_stats_clear(s_cycled_stats[i]);
}
