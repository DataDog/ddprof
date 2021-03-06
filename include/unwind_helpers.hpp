// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "common_symbol_errors.hpp"
#include "ddprof_defs.hpp"
#include "dso.hpp"
#include "symbol_hdr.hpp"
#include <string_view>

typedef struct UnwindState UnwindState;

namespace ddprof {
bool max_stack_depth_reached(UnwindState *us);

void add_common_frame(UnwindState *us, SymbolErrors lookup_case);

void add_dso_frame(UnwindState *us, const Dso &dso, ProcessAddress_t pc,
                   std::string_view addr_type);

void add_virtual_base_frame(UnwindState *us);

bool memory_read(ProcessAddress_t addr, ElfWord_t *result, int regno,
                 void *arg);

void add_error_frame(const Dso *dso, UnwindState *us, ProcessAddress_t pc,
                     SymbolErrors error_case = SymbolErrors::unknown_dso);

} // namespace ddprof
