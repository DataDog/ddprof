#pragma once

#include <stddef.h>

#include "ddprof_context.h"
#include "ddres_def.h"
#include "unwind_output.h"

// forward declarations
typedef struct DDReq DDReq;
typedef struct DProf DProf;

DDRes ddprof_export(DDProfContext *ctx, int64_t now);

// to be replaced
void ddprof_aggregate(const UnwindOutput *uw_output, uint64_t sample_period,
                      int pos, int num_watchers, DProf *dp);
