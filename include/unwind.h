#pragma once

#include "unwind_metrics.h"

#include <sys/types.h>

DDRes unwind_init(struct UnwindState *);
void unwind_free(struct UnwindState *);
DDRes unwindstate__unwind(struct UnwindState *us);

void unwind_cycle(struct UnwindState *us);

// Clear unwinding structures of this pid
void unwind_pid_free(struct UnwindState *us, pid_t pid);
