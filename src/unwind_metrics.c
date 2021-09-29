#include "unwind_metrics.h"

static const DDPROF_STATS s_cycled_stats[] = {STATS_UNWIND_FRAMES,
                                              STATS_UNWIND_ERRORS};

#define cycled_stats_sz (sizeof(s_cycled_stats) / sizeof(DDPROF_STATS))
void unwind_metrics_reset(void) {
  for (unsigned i = 0; i < cycled_stats_sz; ++i)
    ddprof_stats_clear(s_cycled_stats[i]);
}
