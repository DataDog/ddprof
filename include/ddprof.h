#pragma once

#include "ddres_def.h"
#include "stack_handler.h"

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;

/*************************  Context setup / free  **************************/
DDRes ddprof_ctx_set(const DDProfInput *input, DDProfContext *);
void ddprof_ctx_free(DDProfContext *);

#ifndef DDPROF_NATIVE_LIB
/*************************  Instrumentation Helpers  **************************/
// Attach a profiler exporting results from a different fork
void ddprof_attach_profiler(DDProfContext *, int);
#endif

// Stack handler should remain valid
void ddprof_attach_handler(DDProfContext *, const StackHandler *stack_handler,
                           int);
