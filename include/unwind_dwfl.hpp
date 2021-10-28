#pragma once

#include "ddres_def.h"
#include "dwfl_hdr.hpp"
#include "unwind_state.h"

namespace ddprof {

DDRes unwind_dwfl(UnwindState *us, DwflWrapper &dwfl_wrapper);

} // namespace ddprof
