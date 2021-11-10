// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol.hpp"

#include <llvm/Demangle/Demangle.h>

extern "C" {
#include "dwfl_internals.h"
#include "logger.h"
}
#include <cassert>

namespace ddprof {
#ifdef DEBUG
static void check_range_assumption(const char *symname, ElfAddress_t mod_addr,
                                   ElfAddress_t elf_addr, size_t elf_size,
                                   ElfAddress_t newpc, ElfAddress_t bias) {
  LG_NFO("WO VMA lsym.from=%lx, lsym.to=%lx (bias=%lx) symname=%s", elf_addr,
         elf_addr + elf_size - 1, bias, symname);
  if ((newpc >= mod_addr + elf_addr) &&
      (newpc <= mod_addr + elf_addr + elf_size + k_min_symbol_size)) {
    LG_NFO("DWFL: WARNING -- YEAH IN NORMALIZED RANGE");
  } else if ((newpc >= elf_addr) &&
             (newpc <= elf_addr + elf_size + k_min_symbol_size)) {
    LG_NFO("DWFL: WARNING -- OH IN PROCESS RANGE ! STILL YEAH !!");
  } else {
    LG_NFO("DWFL: WARNING -- ERROR NOTHING MAKES SENSE, RUN");
  }
}
#endif

// compute the info using dwarf and demangle APIs
bool symbol_get_from_dwfl(Dwfl_Module *mod, ProcessAddress_t process_pc,
                          Symbol &symbol, GElf_Sym &elf_sym, Offset_t &lbias) {
  // sym not used in the rest of the process : not storing it
  GElf_Word lshndxp;
  Elf *lelfp;
  GElf_Off loffset;
  bool symbol_success = false;
  const char *lsymname = dwfl_module_addrinfo(
      mod, process_pc, &loffset, &elf_sym, &lshndxp, &lelfp, &lbias);

  if (lsymname) {
    symbol._symname = std::string(lsymname);
    symbol._demangle_name = llvm::demangle(symbol._symname);
#ifdef DEBUG
    check_range_assumption(symbol._symname.c_str(), mod->low_addr,
                           elf_sym.st_value, elf_sym.st_size, process_pc,
                           lbias);
#endif
    symbol_success = true;
  }

// #define FLAG_SYMBOL
// A small mechanism to create a trace around the expected function
#ifdef FLAG_SYMBOL
  static const std::string look_for_symb = "pprof_aggregate";
  if (symbol._demangle_name.find(look_for_symb) != std::string::npos) {
    LG_NFO("DGB:: GOING THROUGH EXPECTED FUNC: %s", look_for_symb.c_str());
  }
#endif

  Dwfl_Line *line = dwfl_module_getsrc(mod, process_pc);
  // srcpath
  int linep;
  const char *localsrcpath =
      dwfl_lineinfo(line, &process_pc, static_cast<int *>(&linep), 0, 0, 0);
  if (localsrcpath) {
    symbol._srcpath = std::string(localsrcpath);
    symbol._lineno = static_cast<uint32_t>(linep);
  } else {
    symbol._lineno = 0;
  }
  return symbol_success;
}

// Compute the start and end addresses in the scope of a region for this symbol
bool compute_elf_range(RegionAddress_t region_pc, ProcessAddress_t mod_lowaddr,
                       Offset_t dso_offset, const GElf_Sym &elf_sym,
                       Offset_t bias, RegionAddress_t &start_sym,
                       RegionAddress_t &end_sym) {
  // Success from dwarf symbolization
  // The elf symbols can be in process address or within a region's address
  // (depending on biais). We need to adapt this range.
  // Also the given size can be 0, we will adjust depending on where the current
  // PC is.
  // clang-format off
  /*
    
    We want to make sure we use only addesses in the scope of a region
                            <---- Region ---><-Reg2-->
                        dso(start)  PC     dso(end)
                           ^        ^
  <---------------> <-----------       mod  --------->
                 mod (low) : a mod is in the context of a file
                  ^
  <---------------><------>       <--->
       biais        offset       ^
                                elf
   The elf address can be given either in the context of the file
   or in the context of the process. It can be adjusted with the bias and
   offset. mod - bias is always the value to the start of the file.
 */
  // clang-format on
  assert(mod_lowaddr >= bias);
  // adjust to region (biais is 0 if we are in a process address)
  ElfAddress_t elf_adjust = mod_lowaddr - bias + dso_offset;
  assert(elf_sym.st_value >= elf_adjust);

  start_sym = elf_sym.st_value - elf_adjust;
  // size can be 0 consider a min offset
  Offset_t end_offset = elf_sym.st_size + k_min_symbol_size - 1;
  end_sym = elf_sym.st_value + end_offset - elf_adjust;

#ifdef DEBUG
  if (region_pc > end_sym) {
    LG_DBG("[SYMBOL] BUMPING RANGE %lx -> %lx (%lx)", start_sym, end_sym,
           region_pc);
  }
#endif

  if (region_pc > end_sym + k_max_symbol_size || region_pc < start_sym) {
    LG_DBG("[SYMBOL] ERROR IN RANGE %lx -> %lx (%lx)", start_sym, end_sym,
           region_pc);
    // Avoid bumping end symbol to an insane range
    return false;
  }
  // if we do not cover this PC, bump it
  end_sym = std::max(end_sym, region_pc);

  return true;
}

} // namespace ddprof