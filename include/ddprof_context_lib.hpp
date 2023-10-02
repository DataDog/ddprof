// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
#include "ddprof_cli.hpp"
#include "ddprof_context.hpp"
#include "ddres_def.hpp"

namespace ddprof {

struct DDProfInput;
struct PerfWatcher;

/***************************** Context Management *****************************/
DDRes context_set(const DDProfCLI &ddprof_cli, DDProfContext &ctx);

int context_allocation_profiling_watcher_idx(const DDProfContext &ctx);

} // namespace ddprof
