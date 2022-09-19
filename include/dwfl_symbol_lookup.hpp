// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "ddprof_module.hpp"
#include "dso.hpp"
#include "dso_symbol_lookup.hpp"
#include "hash_helper.hpp"
#include "symbol_map.hpp"
#include "symbol_table.hpp"

#include <iostream>
#include <unordered_map>

typedef struct Dwfl Dwfl;
typedef struct Dwfl_Module Dwfl_Module;

namespace ddprof {

// forward declare to avoid pulling in dwfl_internals in the header
struct DwflWrapper;

struct DwflSymbolLookupStats {
  DwflSymbolLookupStats()
      : _hit(0), _calls(0), _errors(0), _no_dwfl_symbols(0) {}
  void reset();
  void display(unsigned nb_elts) const;
  int _hit;
  int _calls;
  int _errors;
  int _no_dwfl_symbols;
};

/*********************/
/* Main lookup class */
/*********************/

class DwflSymbolLookup {
public:
  // build and check env var to know check setting
  DwflSymbolLookup();

  // Get symbol from internal cache or fetch through dwarf
  SymbolIdx_t get_or_insert(const DDProfMod &ddprof_mod, SymbolTable &table,
                            DsoSymbolLookup &dso_symbol_lookup,
                            FileInfoId_t file_id, ProcessAddress_t process_pc,
                            const Dso &dso);

  void erase(FileInfoId_t file_info_id) { _file_info_map.erase(file_info_id); }

  DwflSymbolLookupStats _stats;

  unsigned size() const;

private:
  /// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
  typedef enum SymbolLookupSetting {
    K_CACHE_ON = 0,
    K_CACHE_VALIDATE,
  } SymbolLookupSetting;

  SymbolLookupSetting _lookup_setting;

  SymbolIdx_t insert(const DDProfMod &ddprof_mod, SymbolTable &table,
                     DsoSymbolLookup &dso_symbol_lookup,
                     ProcessAddress_t process_pc, const Dso &dso,
                     SymbolMap &map);

  // Symbols are ordered by file.
  // The assumption is that the elf addresses are the same across processes
  // The unordered map stores symbols per file,
  // The map stores symbols per address range
  using FileInfo2SymbolMap = std::unordered_map<FileInfoId_t, SymbolMap>;
  using FileInfo2SymbolVT = FileInfo2SymbolMap::value_type;

  static bool symbol_lookup_check(Dwfl_Module *mod, ElfAddress_t process_pc,
                                  const Symbol &info);

  // unordered map of DSO elements
  FileInfo2SymbolMap _file_info_map;
};

} // namespace ddprof
