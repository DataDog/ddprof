// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol.hpp"

#include "dwfl_internals.hpp"
//#include "elfutils/dwarf.h"
#include "logger.hpp"

#include <cassert>
#include <demangler/demangler.hpp>
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
    LG_DBG("[DWFL_SYMB] addrinfo error -- Error:%s -- %lx",
           dwfl_errmsg(dwfl_error_value), process_pc);
  }
#else
  dwfl_errno();
#endif

  if (lsymname) {
    symbol._symname = std::string(lsymname);
    symbol._demangle_name = Demangler::demangle(symbol._symname);
    symbol_success = true;
  } else {
    return false;
  }

// #define FLAG_SYMBOL
// A small mechanism to create a trace around the expected function
#ifdef FLAG_SYMBOL
  static constexpr std::string_view look_for_symb = "$x";
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

  if (line) {
    int linep;
    const char *localsrcpath =
        dwfl_lineinfo(line, &process_pc, static_cast<int *>(&linep), 0, 0, 0);
    if (localsrcpath) {
      symbol._srcpath = std::string(localsrcpath);
      symbol._lineno = static_cast<uint32_t>(linep);
    }
    LG_DBG("Lets try from line %u / %s / %s / %lx / %lx\n",
           symbol._lineno,
           symbol._srcpath.c_str(),
           symbol._demangle_name.c_str(), process_pc, lbias);
#ifdef DEBUG
    dwfl_error_value = dwfl_errno();
    if (unlikely(dwfl_error_value)) {
      LG_DBG("[DWFL_SYMB] dwfl_lineinfo error pc=%lx : Error:%s (Sym=%s)",
             process_pc, dwfl_errmsg(dwfl_error_value),
             symbol._demangle_name.c_str());
    }
#else
    dwfl_errno();
#endif
    Dwarf_Addr addr = process_pc;  // The address at which you want to check inlining information.
    Dwarf_Die *cudie = dwfl_linecu(line); // The compilation unit DIE.
    Dwarf_Die *scopes;
    int nscopes = dwarf_getscopes_die(cudie, &scopes);
    LG_DBG("Found %d scopes \n", nscopes);
    for (int i = 0; i < nscopes; ++i) {
      LG_DBG("dwarf tag = %d - %s\n", dwarf_tag(&scopes[i]),
             dwarf_diename(&scopes[i]));
    }
    Dwarf_Addr dw_bias;
    Dwarf *dwarf = dwfl_module_getdwarf(mod, &dw_bias);
    Dwarf_Line *dwarf_line = dwfl_dwarf_line (line, &dw_bias);
    LG_DBG("dwarf %p - dwarf_line %p\n", dwarf, dwarf_line);
    if (dwarf && line) {
      const char *inline_func_name = dwarf_linefunctionname(dwarf, dwarf_line);
      LG_DBG("inlined func = %p\n", inline_func_name);
      if (inline_func_name) {
        LG_DBG("Inlined function %s \n", inline_func_name);
      }
    }
  }
  return symbol_success;
}

bool compute_elf_range(ElfAddress_t file_pc, const GElf_Sym &elf_sym,
                       ElfAddress_t &start_sym, ElfAddress_t &end_sym) {

  start_sym = elf_sym.st_value;
  if (elf_sym.st_size) {
    end_sym = elf_sym.st_value + elf_sym.st_size - 1;
  } else {
    end_sym = elf_sym.st_value + k_min_symbol_size;
  }

  return file_pc >= start_sym && file_pc <= end_sym;
}

} // namespace ddprof
