// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "ddprof_module.hpp"
#include "ddres.hpp"
#include "dso.hpp"
#include "dso_symbol_lookup.hpp"
#include "hash_helper.hpp"
#include "symbol_map.hpp"
#include "symbol_table.hpp"
#include "unwind_output.hpp"

#include <iostream>
#include <unordered_map>

using Dwfl = struct Dwfl;
using Dwfl_Module = struct Dwfl_Module;

namespace ddprof {

// forward declare to avoid pulling in dwfl_internals in the header
struct DwflWrapper;

struct DwflSymbolLookupStats {
  DwflSymbolLookupStats() = default;

  void reset();
  void display(unsigned nb_elts) const;
  int _hit{0};
  int _calls{0};
  int _errors{0};
  int _no_dwfl_symbols{0};
};

/*********************/
/* Main lookup class */
/*********************/

class DwflSymbolLookup {
public:
  using SymbolRange = std::pair<ElfAddress_t, ElfAddress_t>;
  // build and check env var to know check setting
  DwflSymbolLookup(bool inlining = true);

  // Get symbol from internal cache or fetch through dwarf
  void get_or_insert(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                     SymbolTable &table, DsoSymbolLookup &dso_symbol_lookup,
                     FileInfoId_t file_info_id, ProcessAddress_t process_pc,
                     const Dso &dso, std::vector<FunLoc> &func_locs);

  void erase(FileInfoId_t file_info_id) {
    _file_info_function_map.erase(file_info_id);
  }

  unsigned size() const;

  const DwflSymbolLookupStats &stats() const { return _stats; }
  DwflSymbolLookupStats &stats() { return _stats; }

  // todo: we can have a better type than symbol idx for the line
  using InlineMap = NestedSymbolMap;
  struct SymbolWrapper {
    LineMap _line_map;
    SymbolMap _symbol_map;
    InlineMap _inline_map;
  };

private:
  /// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
  enum SymbolLookupSetting {
    K_CACHE_ON = 0,
    K_CACHE_VALIDATE,
  };

  SymbolLookupSetting _lookup_setting{K_CACHE_ON};

  SymbolMap::ValueType &insert(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                               SymbolTable &table,
                               DsoSymbolLookup &dso_symbol_lookup,
                               ProcessAddress_t process_pc, const Dso &dso,
                               SymbolWrapper &symbol_wrapper);

  void add_fun_loc(DwflSymbolLookup::SymbolWrapper &symbol_wrapper,
                   const SymbolMap::ValueType &parent_sym, ElfAddress_t elf_pc,
                   ProcessAddress_t process_pc, std::vector<FunLoc> &func_locs);

  static DDRes insert_inlining_info(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                                    SymbolTable &table,
                                    ProcessAddress_t process_pc, const Dso &dso,
                                    SymbolWrapper &symbol_wrapper,
                                    SymbolMap::ValueType &parent_func);

  static NestedSymbolMap::FindRes
  get_inlined(SymbolWrapper &symbol_wrapper, ElfAddress_t process_pc,
              ElfAddress_t elf_pc, const SymbolMap::ValueType &parent_sym,
              std::vector<FunLoc> &func_locs);

  // Symbols are ordered by file.
  // The assumption is that the elf addresses are the same across processes
  // The unordered map stores symbols per file,
  // The map stores symbols per address range
  using FileInfo2SymbolWrapper =
      std::unordered_map<FileInfoId_t, SymbolWrapper>;
  using FileInfo2LineMap = std::unordered_map<FileInfoId_t, LineMap>;
  using FileInfo2SymbolVT = FileInfo2SymbolWrapper::value_type;

  static bool symbol_lookup_check(Dwfl_Module *mod, ElfAddress_t process_pc,
                                  const Symbol &symbol);

  // unordered map of DSO elements
  FileInfo2SymbolWrapper _file_info_function_map;
  FileInfo2LineMap _file_info_inlining_map;
  DwflSymbolLookupStats _stats;
  bool _inlining;
};

} // namespace ddprof
