// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.h"
#include "unwind_state.hpp"

namespace ddprof {

DDRes unwind_dwfl(UnwindState *us, DwflWrapper &dwfl_wrapper);

} // namespace ddprof
