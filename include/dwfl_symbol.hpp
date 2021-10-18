#pragma once

#include "ddprof_defs.h"
#include "symbol.hpp"

extern "C" {
#include "gelf.h"
}

struct Dwfl_Module;

namespace ddprof {
// Min in case the size of the elf is given as 0 (TBD: is this low enough ?)
static const Offset_t k_min_symbol_size = 8;
// Max used to make assumption on the cache ranges we should consider
static const Offset_t k_max_symbol_size = 80;

// get symbol from dwarf for this mod
bool symbol_get_from_dwfl(Dwfl_Module *mod, ProcessAddress_t process_pc,
                          Symbol &symbol, GElf_Sym &elf_sym, Offset_t &lbias);

// Compute the start and end addresses in the scope of a region for this symbol
bool compute_elf_range(RegionAddress_t region_pc, ProcessAddress_t mod_lowaddr,
                       Offset_t dso_offset, const GElf_Sym &elf_sym,
                       Offset_t bias, RegionAddress_t &start_sym,
                       RegionAddress_t &end_sym);
} // namespace ddprof
