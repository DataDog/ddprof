// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.h"

typedef struct UnwindState UnwindState;
namespace ddprof {

DDRes unwind_init_dwfl(UnwindState *us);

DDRes unwind_dwfl(UnwindState *us);

} // namespace ddprof
