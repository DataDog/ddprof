#pragma once

#include "ddprof_defs.h"
#include "dso.hpp"
#include "dso_symbol_lookup.hpp"
#include "hash_helper.hpp"
#include "symbol_table.hpp"

#include <iostream>
#include <map>
#include <unordered_map>

struct Dwfl_Module;

namespace ddprof {

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

using DwflSymbolKey_V2 = Offset_t;

class DwflSymbolVal_V2 {
public:
  DwflSymbolVal_V2(Offset_t end, SymbolIdx_t symbol_idx, DsoUID_t dso_uid)
      : _end(end), _symbol_idx(symbol_idx), _dso_id(dso_uid) {}
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
  DsoUID_t _dso_id;
};

using DwflSymbolMap = std::map<Offset_t, DwflSymbolVal_V2>;
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
  SymbolIdx_t get_or_insert(SymbolTable &table,
                            DsoSymbolLookup &dso_symbol_lookup,
                            Dwfl_Module *mod, ElfAddress_t newpc,
                            const Dso &dso);

  void erase(DsoUID_t dso_uid) { _dso_map.erase(dso_uid); }

  DwflSymbolLookupStats _stats;

  unsigned size() const;

  static const unsigned k_sym_min_size = 8;

private:
  /// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
  typedef enum SymbolLookupSetting {
    K_CACHE_ON = 0,
    K_CACHE_VALIDATE,
  } SymbolLookupSetting;

  SymbolLookupSetting _lookup_setting;

  static bool dwfl_symbol_is_within(const Offset_t &norm_pc,
                                    const DwflSymbolMapValueType &kv);
  static DwflSymbolMapFindRes find_closest(DwflSymbolMap &map,
                                           Offset_t norm_pc);

  // Unique ID representing a DSO
  // I (r1viollet) am not using PIDs as I reuse DSOs between
  // PIDs If we are sure the underlying symbols are the same, we can point
  // For short lived forks, this can avoid repopulating caches
  using DwflDsoSymbolMap = std::unordered_map<DsoUID_t, DwflSymbolMap>;
  using DwflDsoSymbolMapVT = DwflDsoSymbolMap::value_type;

  static bool symbol_lookup_check(struct Dwfl_Module *mod, ElfAddress_t newpc,
                                  const Symbol &info);

  // unordered map of DSO elements
  DwflDsoSymbolMap _dso_map;
};

} // namespace ddprof
