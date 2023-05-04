// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_context.hpp"
#include "ddres_def.hpp"
#include <cstddef>

namespace ddprof {

struct Preset {
  static constexpr size_t k_max_events = 10;
  const char *name;
  const char *events[k_max_events];
};

DDRes add_preset(DDProfContext *ctx, const char *preset,
                 bool pid_or_global_mode);

} // namespace ddprof
