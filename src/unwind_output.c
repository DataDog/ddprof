#include <stdio.h>
#include <string.h>

#include "unwind_output.h"

static void FunLoc_clear(FunLoc *locs) {
  memset(locs, 0, sizeof(*locs) * DD_MAX_STACK);
}

void uw_output_clear(UnwindOutput *output) {
  FunLoc_clear(output->locs);
  output->idx = 0;
}
