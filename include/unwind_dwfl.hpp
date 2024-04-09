// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_process.hpp"
#include "ddres_def.hpp"

namespace ddprof {

struct UnwindState;

DDRes unwind_init_dwfl(Process &process, UnwindState *us);

DDRes unwind_dwfl(Process &process, bool avoid_new_attach, UnwindState *us);

} // namespace ddprof
