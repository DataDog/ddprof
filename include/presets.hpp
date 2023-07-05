// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"
#include "perf_watcher.hpp"
#include <cstddef>
#include <string_view>
#include <vector>

namespace ddprof {

struct Preset {
  const char *name;
  const char *events;
};

DDRes add_preset(std::string_view preset, bool pid_or_global_mode,
                 uint32_t default_sample_stack_user,
                 std::vector<PerfWatcher> &watchers);

} // namespace ddprof
