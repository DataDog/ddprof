#pragma once

#include <stdint.h>

#include "ddprof_defs.h"
#include "string_view.h"

typedef struct FunLoc {
  uint64_t ip; // Relative to file, not VMA
  IPInfoIdx_t _ipinfo_idx;
  MapInfoIdx_t _map_info_idx;
} FunLoc;

typedef struct UnwindOutput {
  FunLoc locs[DD_MAX_STACK];
  uint64_t nb_locs;
} UnwindOutput;

void uw_output_clear(UnwindOutput *);
