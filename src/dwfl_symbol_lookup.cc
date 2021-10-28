#include "dwfl_symbol_lookup.hpp"

extern "C" {
#include "dwfl_internals.h"
#include "logger.h"
}

#include <algorithm>
#include <cassert>
#include <string>

#include "dwfl_symbol.hpp"
#include "string_format.hpp"

namespace ddprof {

DwflSymbolLookup_V2::DwflSymbolLookup_V2() : _lookup_setting(K_CACHE_ON) {
  if (const char *env_p = std::getenv("DDPROF_CACHE_SETTING")) {
    if (strcmp(env_p, "VALIDATE") == 0) {
      // Allows to compare the accuracy of the cache
      _lookup_setting = K_CACHE_VALIDATE;
      LG_NTC("%s : Validate the cache data at every call", __FUNCTION__);
    } else {
      LG_WRN("%s : ignoring DDPROF_CACHE_SETTING value %s", __FUNCTION__,
             env_p);
    }
  }
}

unsigned DwflSymbolLookup_V2::size() const {
  unsigned total_nb_elts = 0;
  std::for_each(
      _dso_map.begin(), _dso_map.end(),
      [&](DwflDsoSymbolMapVT const &el) { total_nb_elts += el.second.size(); });
  return total_nb_elts;
}

#ifndef DWFL_CACHE_AS_MAP
/****************/
/*    HASHMAP   */
/****************/
// Simple matching of PC --> symbol
SymbolIdx_t DwflSymbolLookup_V2::get_or_insert(
    SymbolTable &table, DsoSymbolLookup &dso_symbol_lookup, Dwfl_Module *mod,
    ElfAddress_t process_pc, const Dso &dso) {
  ++_stats._calls;
  RegionAddress_t region_pc = (process_pc - dso._start);
  DsoUID_t dso_uid = dso._id;
  DwflSymbolMap &map = _dso_map[dso_uid];
  DwflSymbolMapIt find_res = map.find(region_pc);

  if (find_res != map.end()) { // already found the correct symbol
    if (_lookup_setting == K_CACHE_VALIDATE) {
      if (symbol_lookup_check(mod, process_pc,
                              table[find_res->second.get_symbol_idx()])) {
        ++_stats._errors;
      }
    }
    ++_stats._hit;
    return find_res->second.get_symbol_idx();
  }

  Symbol symbol;
  GElf_Sym elf_sym;
  Dwarf_Addr lbias;
  symbol_get_from_dwfl(mod, process_pc, symbol, elf_sym, lbias);

  if (symbol._symname.empty()) {
    ++_stats._no_dwfl_symbols;
    SymbolIdx_t symbol_idx =
        dso_symbol_lookup.get_or_insert(process_pc, dso, table);
    map.emplace(region_pc, DwflSymbolVal_V2(0, symbol_idx, dso_uid));
    return symbol_idx;
  }

  // success from symbolization
  SymbolIdx_t symbol_idx = table.size();
  if (symbol._srcpath.empty()) {
    // override with info from dso (this slightly mixes mappings and sources)
    // But it helps a lot with colors (as mappings are ignored for now in UI)
    symbol._srcpath = dso.format_filename();
  }

  map.emplace(region_pc, DwflSymbolVal_V2(0, symbol_idx, dso_uid));
  table.push_back(std::move(symbol));
  return symbol_idx;
}
#else
/****************/
/* Range implem */
/****************/

// Retrieve existing symbol or attempt to read from dwarf
SymbolIdx_t DwflSymbolLookup_V2::get_or_insert(
    SymbolTable &table, DsoSymbolLookup &dso_symbol_lookup, Dwfl_Module *mod,
    ElfAddress_t process_pc, const Dso &dso) {
  ++_stats._calls;
  RegionAddress_t region_pc = process_pc - dso._start;
#  ifndef NDEBUG
  // This assumption guarantees that we can use DSO / Mod toghether
  if (dso._start - dso._pgoff != mod->low_addr) {
    LG_ERR("[SYMB] Failed to check mod vs dso: %lx = (%lx - %lx) / (offset : "
           "%lx) / dso:%s / mod=%lx",
           region_pc, process_pc, dso._start, dso._pgoff, dso._filename.c_str(),
           mod->low_addr);
    assert(false);
  }
#  endif
#  ifdef DEBUG
  LG_DBG("Looking for : %lx = (%lx - %lx) / (offset : %lx) / dso:%s", region_pc,
         process_pc, dso._start, dso._pgoff, dso._filename.c_str());
#  endif
  DsoUID_t dso_uid = dso._id;
  DwflSymbolMap &map = _dso_map[dso_uid];
  DwflSymbolMapFindRes find_res = find_closest(map, region_pc);
  if (find_res.second) { // already found the correct symbol
#  ifdef DEBUG
    LG_DBG("Match : %lx,%lx -> %s,%d,%d", find_res.first->first,
           find_res.first->second.get_end(),
           table[find_res.first->second.get_symbol_idx()]._symname.c_str(),
           dso_uid, find_res.first->second.get_symbol_idx());
#  endif
    // cache validation mechanism: force dwfl lookup to compare with matched
    // symbols
    if (_lookup_setting == K_CACHE_VALIDATE) {
      if (symbol_lookup_check(mod, process_pc,
                              table[find_res.first->second.get_symbol_idx()])) {
        ++_stats._errors;
      }
    }
    ++_stats._hit;
    return find_res.first->second.get_symbol_idx();
  }

  Symbol symbol;
  GElf_Sym elf_sym;
  Offset_t lbias;

  if (!symbol_get_from_dwfl(mod, process_pc, symbol, elf_sym, lbias)) {
    ++_stats._no_dwfl_symbols;
    // Override with info from dso
    // Avoid bouncing on these requests and insert an element

    Offset_t start_sym = region_pc;
    Offset_t end_sym = region_pc + 1;

    SymbolIdx_t symbol_idx =
        dso_symbol_lookup.get_or_insert(process_pc, dso, table);
#  ifdef DEBUG
    LG_DBG("Insert (dwfl failure): %lx,%lx -> %s,%d,%d", start_sym, end_sym,
           table[symbol_idx]._symname.c_str(), dso_uid, symbol_idx);
#  endif
    map.emplace(start_sym, DwflSymbolVal_V2(end_sym, symbol_idx, dso_uid));

    return symbol_idx;
  }

  // Check if the closest element is the same
  if (find_res.first != map.end()) {
    // if it is the same -> extend the end to current pc
    // Note: I have never hit this. Perhaps it can be removed (TBD)
    SymbolIdx_t previous_symb = find_res.first->second.get_symbol_idx();
    if (symbol._demangle_name == table[previous_symb]._demangle_name) {
      find_res.first->second.set_end(region_pc);
#  ifdef DEBUG
      LG_DBG("Reuse previously matched %lx,%lx -> %s,%d,%d",
             find_res.first->first, region_pc, symbol._symname.c_str(), dso_uid,
             previous_symb);
#  endif
      return previous_symb;
    }
  }

  RegionAddress_t start_sym;
  RegionAddress_t end_sym;
  // All paths bellow will evetually insert symbol in the table
  SymbolIdx_t symbol_idx = table.size();
  if (symbol._srcpath.empty()) {
    // override with info from dso (this slightly mixes mappings and sources)
    // But it helps a lot with colors (as mappings are ignored for now in UI)
    symbol._srcpath = dso.format_filename();
  }

  if (!compute_elf_range(region_pc, mod->low_addr, dso._pgoff, elf_sym, lbias,
                         start_sym, end_sym)) {
    // elf section does not add up to something that makes sense
    // insert this PC without considering elf section
    start_sym = region_pc;
    end_sym = region_pc + 1;
#  ifdef DEBUG
    LG_DBG("Insert: %lx,%lx -> %s,%d,%d / shndx=%d", start_sym, end_sym,
           symbol._symname.c_str(), dso_uid, symbol_idx, elf_sym.st_shndx);
#  endif
    map.emplace(start_sym, DwflSymbolVal_V2(end_sym, symbol_idx, dso_uid));
    table.push_back(std::move(symbol));
    return symbol_idx;
  }

#  ifdef DEBUG
  LG_DBG("Insert: %lx,%lx -> %s,%d,%d / shndx=%d", start_sym, end_sym,
         symbol._symname.c_str(), dso_uid, symbol_idx, elf_sym.st_shndx);
#  endif
  map.emplace(start_sym, DwflSymbolVal_V2(end_sym, symbol_idx, dso_uid));
  table.push_back(std::move(symbol));
  return symbol_idx;
}

DwflSymbolMapFindRes DwflSymbolLookup_V2::find_closest(DwflSymbolMap &map,
                                                       Offset_t norm_pc) {
  bool is_within = false;

  // First element not less than (can match exactly a start addr)
  DwflSymbolMapIt it = map.lower_bound(norm_pc);
  if (it != map.end()) { // map is empty
    is_within = dwfl_symbol_is_within(norm_pc, *it);
    if (is_within) {
      return std::make_pair<DwflSymbolMapIt, bool>(std::move(it),
                                                   std::move(is_within));
    }
  }

  // previous element is more likely to contain our addr
  if (it != map.begin()) {
    --it;
  } else { // map is empty
    return std::make_pair<DwflSymbolMapIt, bool>(map.end(), false);
  }
  // element can not be end (as we reversed or exit)
  is_within = dwfl_symbol_is_within(norm_pc, *it);

  return std::make_pair<DwflSymbolMapIt, bool>(std::move(it),
                                               std::move(is_within));
}
#endif

bool DwflSymbolLookup_V2::symbol_lookup_check(struct Dwfl_Module *mod,
                                              Dwarf_Addr process_pc,
                                              const Symbol &symbol) {

  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(
      mod, process_pc, &loffset, &lsym, &lshndxp, &lelfp, &lbias);

#ifdef DEBUG
  LG_DBG("DWFL: Lookup res = %lx->%lx, shndx=%u, biais=%lx, elfp=%p, "
         "shndxp=%u, %s",
         lsym.st_value, lsym.st_value + lsym.st_size, lsym.st_shndx, lbias,
         lelfp, lshndxp, localsymname);
#endif

  bool error_found = false;
  if (!localsymname) { // symbol failure no use checking
    return error_found;
  }

  if (symbol._symname.empty()) {
    LG_ERR("Error from cache : non null symname = %s", localsymname);
  } else {
    if (strcmp(symbol._symname.c_str(), localsymname) != 0) {
      LG_ERR("Error from cache symname Real=%s vs Cache=%s ", localsymname,
             symbol._symname.c_str());
      error_found = true;
    }
    if (error_found) {
      LG_ERR("symname = %s\n", symbol._symname.c_str());
    }
  }
  return error_found;
}

void DwflSymbolLookupStats::display(unsigned nb_elts) const {
  static const int k_cent_precision = 10000;
  if (_calls) {
    LG_NTC("symbol_lookup_stats : Hit / calls = [%d/%d] = %d", _hit, _calls,
           (_hit * k_cent_precision) / _calls);
    if (_errors) {
      LG_WRN("                   Errors / calls = [%d/%d] = %d", _errors,
             _calls, (_errors * k_cent_precision) / _calls);
    }
    if (_no_dwfl_symbols) {
      LG_NTC("no_dwfl_symbols :  Occur / calls = [%d/%d] = %d",
             _no_dwfl_symbols, _calls,
             (_no_dwfl_symbols * k_cent_precision) / _calls);
    }
    LG_NTC("                   Size of cache = %d", nb_elts);
  } else {
    LG_NTC("symbol_lookup_stats : 0 calls");
  }
}

void DwflSymbolLookupStats::reset() {
  _hit = 0;
  _calls = 0;
  _errors = 0;
}

bool DwflSymbolLookup_V2::dwfl_symbol_is_within(
    const Offset_t &norm_pc, const DwflSymbolMapValueType &kv) {
  if (norm_pc < kv.first) {
    return false;
  }
  if (norm_pc > kv.second.get_end()) {
    return false;
  }
  return true;
}

} // namespace ddprof
