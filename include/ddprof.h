#pragma once

#include <sys/types.h>

#include "ddres_def.h"

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;

/*************************  Context setup / free  **************************/
DDRes ddprof_ctx_set(const DDProfInput *input, DDProfContext *);
void ddprof_ctx_free(DDProfContext *);

/*************************  Instrumentation Helpers  **************************/
// Attach a profiler exporting results from a different fork
void ddprof_attach_profiler(DDProfContext *, pid_t, int);
