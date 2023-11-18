// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol.hpp"

#include "dwfl_internals.hpp"
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
  if (lsymname) {
    symbol._symname = std::string(lsymname);
    symbol._demangle_name = Demangler::demangle(symbol._symname);
    symbol_success = true;
  } else {
    // reset error state in case of dwfl error
    dwfl_errno();
    symbol_success = false;
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
