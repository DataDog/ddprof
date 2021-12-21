// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.h"
#include "ddprof_file_info.hpp"
#include "dso.hpp"
#include "dso_symbol_lookup.hpp"
#include "hash_helper.hpp"
#include "symbol_table.hpp"

#include <iostream>
#include <map>
#include <unordered_map>

extern "C" {
typedef struct Dwfl Dwfl;
typedef struct Dwfl_Module Dwfl_Module;
}

namespace ddprof {

#define DWFL_CACHE_AS_MAP

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

using DwflSymbolKey_V2 = RegionAddress_t;

class DwflSymbolVal_V2 {
public:
  DwflSymbolVal_V2(Offset_t end, SymbolIdx_t symbol_idx)
      : _end(end), _symbol_idx(symbol_idx) {}
  // push end further
  void set_end(Offset_t end) {
    if (end > _end) {
      _end = end;
    }
  }
  Offset_t get_end() const { return _end; }

  SymbolIdx_t get_symbol_idx() const { return _symbol_idx; }

private:
  // symbol end within the segment (considering file offset)
  Offset_t _end;
  // element inside internal symbol cache
  SymbolIdx_t _symbol_idx;
};

// Range management allows better performances (and less mem overhead)
using DwflSymbolMap = std::map<RegionAddress_t, DwflSymbolVal_V2>;
using DwflSymbolMapIt = DwflSymbolMap::iterator;
using DwflSymbolMapFindRes = std::pair<DwflSymbolMapIt, bool>;
using DwflSymbolMapValueType =
    DwflSymbolMap::value_type; // key value pair <Offset_t, DwflSymbolVal_V2>;

/*********************/
/* Main lookup class */
/*********************/

class DwflSymbolLookup_V2 {
public:
  // build and check env var to know check setting
  DwflSymbolLookup_V2();

  // Get symbol from internal cache or fetch through dwarf
  SymbolIdx_t get_or_insert(Dwfl *dwfl, SymbolTable &table,
                            DsoSymbolLookup &dso_symbol_lookup,
                            ProcessAddress_t process_pc, const Dso &dso,
                            const FileInfoValue &file_info);

  void erase(FileInfoId_t file_info_id) {
    _file_info_inode_map.erase(file_info_id);
  }

  DwflSymbolLookupStats _stats;

  unsigned size() const;

private:
  /// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
  typedef enum SymbolLookupSetting {
    K_CACHE_ON = 0,
    K_CACHE_VALIDATE,
  } SymbolLookupSetting;

  SymbolLookupSetting _lookup_setting;

  SymbolIdx_t insert(Dwfl *dwfl, SymbolTable &table,
                     DsoSymbolLookup &dso_symbol_lookup,
                     ProcessAddress_t process_pc, const Dso &dso,
                     const FileInfoValue &file_info, DwflSymbolMap &map,
                     DwflSymbolMapFindRes find_res);

  static bool dwfl_symbol_is_within(const Offset_t &norm_pc,
                                    const DwflSymbolMapValueType &kv);
  static DwflSymbolMapFindRes find_closest(DwflSymbolMap &map,
                                           Offset_t norm_pc);

  // Unique ID representing a DSO
  // I (r1viollet) am not using PIDs as I reuse DSOs between
  // PIDs. If we are sure the underlying symbols are the same, we can assume the
  // symbol cache is the same. For short lived forks, this can avoid
  // repopulating caches.
  using FileInfoInodeMap = std::unordered_map<FileInfoId_t, DwflSymbolMap>;
  using FileInfoInodeMapVT = FileInfoInodeMap::value_type;

  static bool symbol_lookup_check(Dwfl_Module *mod, ElfAddress_t process_pc,
                                  const Symbol &info);

  // unordered map of DSO elements
  FileInfoInodeMap _file_info_inode_map;
};

} // namespace ddprof
