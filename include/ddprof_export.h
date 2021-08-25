#pragma once

#include <stddef.h>

#include "ddprof.h"
#include "ddprof_context.h"
#include "unwind_output.h"

DDRes ddprof_export(DDProfContext *ctx, int64_t now);
void ddprof_aggregate(const UnwindOutput *uw_output, uint64_t sample_period,
                      int pos, int num_watchers, DProf *dp);
