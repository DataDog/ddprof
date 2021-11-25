// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "unwind_metrics.h"
#include <sys/types.h>
}
#include "unwind_state.hpp"

namespace ddprof {

void unwind_init(void);

DDRes unwindstate__unwind(UnwindState *us);

void unwind_cycle(UnwindState *us);

// Clear unwinding structures of this pid
void unwind_pid_free(UnwindState *us, pid_t pid);

} // namespace ddprof
