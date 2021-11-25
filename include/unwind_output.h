// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// External API: This file should stay in C

#pragma once

#include <stdint.h>

#include "ddprof_defs.h"
#include "string_view.h"

typedef struct FunLoc {
  uint64_t ip; // Relative to file, not VMA
  SymbolIdx_t _symbol_idx;
  MapInfoIdx_t _map_info_idx;
} FunLoc;

typedef struct UnwindOutput {
  FunLoc locs[DD_MAX_STACK_DEPTH];
  uint64_t nb_locs;
} UnwindOutput;

void uw_output_clear(UnwindOutput *);
