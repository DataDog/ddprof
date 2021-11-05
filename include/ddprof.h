#pragma once

#include "ddres_def.h"
#include "stack_handler.h"

#include <unistd.h>

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;

// Setup perf event open according to watchers
DDRes ddprof_setup(DDProfContext *ctx, pid_t pid);

#ifndef DDPROF_NATIVE_LIB
/*************************  Instrumentation Helpers  **************************/
// Attach a profiler exporting results from a different fork
void ddprof_start_profiler(DDProfContext *);
#endif

// Stack handler should remain valid
void ddprof_attach_handler(DDProfContext *, const StackHandler *stack_handler);
