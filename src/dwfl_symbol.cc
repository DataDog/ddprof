// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol.hpp"

#include "dwfl_internals.hpp"
#include "logger.hpp"

#include <cassert>
#include <llvm/Demangle/Demangle.h>
#include <string_view>

namespace ddprof {

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

#ifdef DEBUG
  int dwfl_error_value = dwfl_errno();
  if (unlikely(dwfl_error_value)) {
    LG_DBG("[DWFL_SYMB] addrinfo error -- Error:%s -- %s",
           dwfl_errmsg(dwfl_error_value), lsymname);
  }
#else
  dwfl_errno();
#endif

  if (lsymname) {
    symbol._symname = std::string(lsymname);
    symbol._demangle_name = llvm::demangle(symbol._symname);
    symbol_success = true;
  } else {
    return false;
  }

// #define FLAG_SYMBOL
// A small mechanism to create a trace around the expected function
#ifdef FLAG_SYMBOL
  static const std::string_view look_for_symb = "runtime.asmcgocall.abi0";
  if (symbol._demangle_name.find(look_for_symb) != std::string::npos) {
    LG_NFO("DGB:: GOING THROUGH EXPECTED FUNC: %s", look_for_symb.data());
  }
#endif
  Dwfl_Line *line = dwfl_module_getsrc(mod, process_pc);
#ifdef DEBUG
  dwfl_error_value = dwfl_errno();
  if (unlikely(dwfl_error_value)) {
    LG_DBG("[DWFL_SYMB] dwfl_src error pc=%lx : Error:%s (Sym=%s)", process_pc,
           dwfl_errmsg(dwfl_error_value), symbol._demangle_name.c_str());
  }
#else
  dwfl_errno();
#endif
  // srcpath
  if (line) {
    int linep;
    const char *localsrcpath =
        dwfl_lineinfo(line, &process_pc, static_cast<int *>(&linep), 0, 0, 0);
    if (localsrcpath) {
      symbol._srcpath = std::string(localsrcpath);
      symbol._lineno = static_cast<uint32_t>(linep);
    }
  }
  return symbol_success;
}

bool compute_elf_range_v2(RegionAddress_t file_pc, const GElf_Sym &elf_sym,
                          RegionAddress_t &start_sym,
                          RegionAddress_t &end_sym) {

  start_sym = elf_sym.st_value;
  if (elf_sym.st_size) {
    end_sym = elf_sym.st_value + elf_sym.st_size - 1;
  } else {
    end_sym = elf_sym.st_value + k_min_symbol_size;
  }
  return file_pc >= start_sym && file_pc <= end_sym;
}

} // namespace ddprof