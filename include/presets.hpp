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
  const char *name;
  const char *events;
};

DDRes add_preset(DDProfContext *ctx, const char *preset,
                 bool pid_or_global_mode);

} // namespace ddprof
