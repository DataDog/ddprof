// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"

namespace ddprof {

struct DDProfContext;
struct PersistentWorkerState;

struct WorkerAttr {
  DDRes (*init_fun)(DDProfContext &ctx, PersistentWorkerState *);
  DDRes (*finish_fun)(DDProfContext &ctx);
};

} // namespace ddprof
