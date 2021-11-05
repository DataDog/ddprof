#pragma once
#include "ddres_def.h"

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;

/***************************** Context Management *****************************/
DDRes ddprof_context_set(const DDProfInput *input, DDProfContext *);
void ddprof_context_free(DDProfContext *);
