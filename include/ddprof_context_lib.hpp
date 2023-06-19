// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
#include "ddres_def.hpp"
#include "ddprof_cli.hpp"
#include "ddprof_context.hpp"

typedef struct DDProfInput DDProfInput;
typedef struct PerfWatcher PerfWatcher;

namespace ddprof {
/***************************** Context Management *****************************/
DDRes context_set(DDProfInput *input, DDProfContext *);
DDRes context_set_v2(const DDProfCLI &ddprof_cli, DDProfContext_V2 &ctx);

void context_free(DDProfContext *);

int context_allocation_profiling_watcher_idx(const DDProfContext *ctx);
} // namespace ddprof
