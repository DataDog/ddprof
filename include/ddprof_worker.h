#pragma once

#include <linux/perf_event.h>
#include <stdbool.h>

#include "ddres.h"

DDRes ddprof_timeout(volatile bool *continue_profiling, void *arg);
DDRes ddprof_worker_init(void *arg);
DDRes ddprof_worker_finish(void *arg, bool is_final);
DDRes ddprof_worker_timeout(volatile bool *continue_profiling, void *arg);
DDRes ddprof_worker(struct perf_event_header *hdr, int pos,
                    volatile bool *continue_profiling, void *arg);
