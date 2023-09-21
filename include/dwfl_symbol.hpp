// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "symbol.hpp"

#include <gelf.h>

struct Dwfl_Module;

namespace ddprof {
// Min in case the size of the elf is given as 0 (TBD: is this low enough ?)
static const Offset_t k_min_symbol_size = 7;
// Max used to make assumption on the cache ranges we should consider
static const Offset_t k_max_symbol_size = 80;

// get symbol from dwarf for this mod
bool symbol_get_from_dwfl(Dwfl_Module *mod, ProcessAddress_t process_pc,
                          Symbol &symbol, GElf_Sym &elf_sym, Offset_t &lbias);

bool compute_elf_range(ElfAddress_t file_pc, const GElf_Sym &elf_sym,
                       ElfAddress_t &start_sym, ElfAddress_t &end_sym);
} // namespace ddprof
