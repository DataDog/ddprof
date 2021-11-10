// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <stdio.h>
#include <string.h>

#include "unwind_output.h"

static void FunLoc_clear(FunLoc *locs) {
  memset(locs, 0, sizeof(*locs) * DD_MAX_STACK_DEPTH);
}

void uw_output_clear(UnwindOutput *output) {
  FunLoc_clear(output->locs);
  output->nb_locs = 0;
}
