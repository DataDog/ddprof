// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"

#include <unistd.h>

namespace ddprof {

struct DDProfInput;
struct DDProfContext;

// Setup perf event open according to watchers
DDRes ddprof_setup(DDProfContext &ctx);

// Free perf event resources
DDRes ddprof_teardown(DDProfContext &ctx);

/*************************  Instrumentation Helpers  **************************/
// Attach a profiler exporting results from a different fork
DDRes ddprof_start_profiler(DDProfContext *ctx);

} // namespace ddprof
