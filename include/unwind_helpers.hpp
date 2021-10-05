#pragma once

extern "C" {
#include "unwind_state.h"
}

#include "unwind_symbols.hpp"

namespace ddprof {
bool max_stack_depth_reached(UnwindState *us);

void add_common_frame(UnwindState *us,
                      CommonSymbolLookup::LookupCases lookup_case);

} // namespace ddprof
