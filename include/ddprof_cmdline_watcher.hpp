// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ddprof {

struct PerfWatcher;

bool watchers_from_str(
    const char *str, std::vector<PerfWatcher> &watchers,
    uint32_t stack_sample_size = k_default_perf_stack_sample_size);

} // namespace ddprof
