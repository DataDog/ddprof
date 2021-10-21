#pragma once

#include "ddres_def.h"
#include "unwind_state.h"

namespace ddprof {

void unwind_dwfl_init(struct UnwindState *us);
void unwind_dwfl_free(struct UnwindState *us);

DDRes unwind_dwfl(UnwindState *us);
} // namespace ddprof
