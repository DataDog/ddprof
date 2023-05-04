// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
#include "ddres_def.hpp"

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;
typedef struct PerfWatcher PerfWatcher;

namespace ddprof {
/***************************** Context Management *****************************/
DDRes context_set(DDProfInput *input, DDProfContext *);
void context_free(DDProfContext *);

int context_allocation_profiling_watcher_idx(const DDProfContext *ctx);
} // namespace ddprof
