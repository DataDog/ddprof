#pragma once

#include "ddres_def.h"
#include "unwind_output.h"

// forward declarations
typedef struct DDReq DDReq;
typedef struct DProf DProf;

/// Write a sample from the unwinding output to the dprof structure
void ddexp_write_sample(const UnwindOutput *uw_output, uint64_t sample_period,
                        int pos, int num_watchers, DProf *dp);

/// Send the profiles
DDRes ddexp_export(DDReq *ddr, DProf *dp, int64_t now);
