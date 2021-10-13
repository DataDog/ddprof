#include "dwfl_symbol_lookup.hpp"

extern "C" {
#include "dwfl_internals.h"
#include "logger.h"
}
#include <algorithm>
#include <cassert>
#include <string>

#include "string_format.hpp"
#include "llvm/Demangle/Demangle.h"

// #define DEBUG

namespace ddprof {

static void symbol_get_from_dwfl(Dwfl_Module *mod, Dwarf_Addr newpc,
                                 Symbol &symbol, GElf_Sym &elf_sym);

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
  std::for_each(_dso_map.begin(), _dso_map.end(),
                [&](auto const &el) { total_nb_elts += el.second.size(); });
  return total_nb_elts;
}

// Retrieve existing symbol or attempt to read from dwarf
SymbolIdx_t DwflSymbolLookup_V2::get_or_insert(
    SymbolTable &table, DsoSymbolLookup &dso_symbol_lookup, Dwfl_Module *mod,
    ElfAddress_t newpc, const Dso &dso) {
  ++_stats._calls;
  Offset_t normalized_pc = (newpc - dso._start) + dso._pgoff;
#ifndef NDEBUG
  if (dso._start - dso._pgoff != mod->low_addr) {
    LG_ERR("Failed to check assumption on mod / dso matching %s",
           dso._filename.c_str());
    assert(false);
  }
#endif
#ifdef DEBUG
  LG_DBG("Looking for : %lx ", normalized_pc);
#endif
  DsoUID_t dso_uid = dso._id;
  DwflSymbolMap &map = _dso_map[dso_uid];
  DwflSymbolMapFindRes find_res = find_closest(map, normalized_pc);
  if (find_res.second) { // already found the correct symbol
#ifdef DEBUG
    LG_DBG("Match : %lx,%lx -> %s,%d,%d", find_res.first->first,
           find_res.first->second.get_end(),
           table[find_res.first->second.get_symbol_idx()]._symname.c_str(),
           dso_uid, find_res.first->second.get_symbol_idx());
#endif
    // cache validation mechanism: force dwfl lookup to compare with matched
    // symbols
    if (_lookup_setting == K_CACHE_VALIDATE) {
      if (symbol_lookup_check(mod, newpc,
                              table[find_res.first->second.get_symbol_idx()])) {
        ++_stats._errors;
      }
    }
    ++_stats._hit;
    return find_res.first->second.get_symbol_idx();
  }

  Symbol symbol;
  GElf_Sym elf_sym;
  symbol_get_from_dwfl(mod, newpc, symbol, elf_sym);

  if (symbol._symname.empty()) {
    ++_stats._no_dwfl_symbols;
    // Override with info from dso
    // Avoid bouncing on these requests and insert an element
    Offset_t start_sym = normalized_pc;
    Offset_t end_sym = normalized_pc + k_sym_min_size;

    SymbolIdx_t symbol_idx = dso_symbol_lookup.get_or_insert(newpc, dso, table);
#ifdef DEBUG
    LG_DBG("Insert (dwfl failure): %lx,%lx -> %s,%d,%d", start_sym, end_sym,
           table[symbol_idx]._symname.c_str(), dso_uid, symbol_idx);
#endif
    map.emplace(start_sym, DwflSymbolVal_V2(end_sym, symbol_idx, dso_uid));

    return symbol_idx;
  } else { // Success from dwarf symbolization
    Offset_t start_sym = elf_sym.st_value;
    if (elf_sym.st_size == 0) { // when size is 0, bump it to min
      elf_sym.st_size += k_sym_min_size;
    }
    Offset_t end_sym =
        std::max(elf_sym.st_value + elf_sym.st_size - 1, normalized_pc);

    // Check if the closest element is the same
    if (find_res.first != map.end()) {
      // if it is the same -> extend the end
      SymbolIdx_t previous_symb = find_res.first->second.get_symbol_idx();

      if (symbol._demangle_name == table[previous_symb]._demangle_name) {
        find_res.first->second.set_end(end_sym);
#ifdef DEBUG
        LG_DBG("Reuse previously matched %lx,%lx -> %s,%d,%d",
               find_res.first->first, end_sym, symbol._symname.c_str(), dso_uid,
               previous_symb);
#endif
        return previous_symb;
      }
    }

    // insert element in map and table
    SymbolIdx_t symbol_idx = table.size();
#ifdef DEBUG
    LG_DBG("Insert: %lx,%lx -> %s,%d,%d / shndx=%d", start_sym, end_sym,
           symbol._symname.c_str(), dso_uid, symbol_idx, elf_sym.st_shndx);
#endif
    if (symbol._srcpath.empty()) {
      // override with info from dso (this slightly mixes mappings and sources)
      // But it helps a lot with colors (as mappings are ignored for now in UI)
      symbol._srcpath = dso.format_filename();
    }

    map.emplace(start_sym, DwflSymbolVal_V2(end_sym, symbol_idx, dso_uid));
    table.push_back(std::move(symbol));
    return symbol_idx;
  }
}

// compute the info using dwarf and demangle APIs
static void symbol_get_from_dwfl(Dwfl_Module *mod, Dwarf_Addr newpc,
                                 Symbol &symbol, GElf_Sym &elf_sym) {
  // sym not used in the rest of the process : not storing it
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;
  GElf_Off loffset;

  const char *lsymname = dwfl_module_addrinfo(mod, newpc, &loffset, &elf_sym,
                                              &lshndxp, &lelfp, &lbias);
  if (lsymname) {

#ifdef DEBUG
    if (!(newpc >= mod->low_addr + elf_sym.st_value) ||
        !(newpc <= mod->low_addr + elf_sym.st_value + elf_sym.st_size +
              DwflSymbolLookup_V2::k_sym_min_size)) {
      LG_NFO("WO VMA lsym.from=%lx, lsym.to=%lx", elf_sym.st_value,
             elf_sym.st_value + elf_sym.st_size);
      LG_NFO("DWFL: WARNING -- BAD ASSUMPTION ON ELF SYMBOL RANGE");
    }
#endif

    symbol._symname = std::string(lsymname);
    symbol._demangle_name = llvm::demangle(symbol._symname);
  }

// #define FLAG_SYMBOL
// A small mechanism to create a trace around the expected function
#ifdef FLAG_SYMBOL
  static const std::string look_for_symb = "pprof_aggregate";
  if (symbol._demangle_name.find(look_for_symb) != std::string::npos) {
    LG_NFO("DGB:: GOING THROUGH EXPECTED FUNC: %s", look_for_symb.c_str());
  }
#endif

  Dwfl_Line *line = dwfl_module_getsrc(mod, newpc);
  // srcpath
  int linep;
  const char *localsrcpath =
      dwfl_lineinfo(line, &newpc, static_cast<int *>(&linep), 0, 0, 0);
  symbol._lineno = static_cast<uint32_t>(linep);
  if (localsrcpath) {
    symbol._srcpath = std::string(localsrcpath);
  }
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

bool DwflSymbolLookup_V2::symbol_lookup_check(struct Dwfl_Module *mod,
                                              Dwarf_Addr newpc,
                                              const Symbol &symbol) {

  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(mod, newpc, &loffset, &lsym,
                                                  &lshndxp, &lelfp, &lbias);

#ifdef DEBUG
  LG_DBG("DWFL: Lookup res = %lx->%lx, shndx=%u, biais=%lx, elfp=%p, shndxp=%u",
         lsym.st_value, lsym.st_value + lsym.st_size, lsym.st_shndx, lbias,
         lelfp, lshndxp);
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
  if (_calls) {
    LG_NTC("symbol_lookup_stats : Hit / calls = [%d/%d] = %d", _hit, _calls,
           (_hit * 100) / _calls);
    if (_errors) {
      LG_WRN("                   Errors / calls = [%d/%d] = %d", _errors,
             _calls, (_errors * 100) / _calls);
    }
    if (_no_dwfl_symbols) {
      LG_NTC("no_dwfl_symbols :  Occur / calls = [%d/%d] = %d",
             _no_dwfl_symbols, _calls, (_no_dwfl_symbols * 100) / _calls);
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

} // namespace ddprof
