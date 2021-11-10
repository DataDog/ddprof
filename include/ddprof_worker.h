// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <linux/perf_event.h>
#include <stdbool.h>

#include "ddres.h"
#include "pevent.h"

typedef struct DDProfContext DDProfContext;

DDRes ddprof_timeout(volatile bool *continue_profiling, DDProfContext *arg);
DDRes ddprof_worker_init(DDProfContext *arg);
DDRes ddprof_worker_finish(DDProfContext *);
DDRes ddprof_worker_timeout(volatile bool *continue_profiling,
                            DDProfContext *arg);
DDRes ddprof_worker(struct perf_event_header *hdr, int pos,
                    volatile bool *continue_profiling, DDProfContext *arg);

// Only init unwinding elements
DDRes worker_unwind_init(DDProfContext *ctx);
DDRes worker_unwind_free(DDProfContext *ctx);
