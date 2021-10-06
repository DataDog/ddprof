#pragma once

extern "C" {
#include "unwind_state.h"
}

#include "dso.hpp"
#include "unwind_symbols.hpp"

namespace ddprof {
bool max_stack_depth_reached(UnwindState *us);

void add_common_frame(UnwindState *us,
                      CommonSymbolLookup::LookupCases lookup_case);

void add_dso_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc);

DDRes add_virtual_base_frame(UnwindState *us);

} // namespace ddprof
