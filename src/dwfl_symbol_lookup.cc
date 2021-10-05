#include "dwfl_symbol_lookup.hpp"

extern "C" {
#include "dwfl_internals.h"
#include "logger.h"
}

#include "string_format.hpp"
#include "llvm/Demangle/Demangle.h"

// #define DEBUG

namespace ddprof {

DwflSymbolKey::DwflSymbolKey(const Dwfl_Module *mod, ElfAddress_t newpc,
                             DsoUID_t dso_id)
    : _low_addr(mod->low_addr), _newpc(newpc), _dso_id(dso_id) {}

// compute the info using dwarf and demangle apis
static void symbol_get_from_dwfl(Dwfl_Module *mod, Dwarf_Addr newpc,
                                 Symbol &symbol) {
  // sym not used in the rest of the process : not storing it
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *lsymname = dwfl_module_addrinfo(mod, newpc, &(symbol._offset),
                                              &lsym, &lshndxp, &lelfp, &lbias);

  if (lsymname) {
    symbol._symname = std::string(lsymname);
    symbol._demangle_name = llvm::demangle(symbol._symname);
  } else {
    symbol._demangle_name = string_format("[%p:dwfl]", newpc - mod->low_addr);
  }

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

// pass through cache
void dwfl_symbol_get_or_insert(DwflSymbolLookup &dwfl_symbol_lookup,
                               DwflSymbolLookupStats &stats, SymbolTable &table,
                               Dwfl_Module *mod, Dwarf_Addr newpc,
                               const Dso &dso, SymbolIdx_t *symbol_idx) {
  DwflSymbolKey key(mod, newpc, dso._id);
  auto const it = dwfl_symbol_lookup.find(key);
  ++(stats._calls);

  if (it != dwfl_symbol_lookup.end()) {
    ++(stats._hit);
#ifdef DEBUG
    printf("DBG: Cache HIT \n");
#endif
    *symbol_idx = it->second;
  } else {

#ifdef DEBUG
    printf("DBG: Cache MISS \n");
#endif
    Symbol symbol;
    symbol_get_from_dwfl(mod, newpc, symbol);
    if (symbol._srcpath.empty()) {
      // override with info from dso
      symbol._srcpath = dso.format_filename();
    }
    *symbol_idx = table.size();
    // This will be similar to an emplace back
    table.push_back(std::move(symbol));

    dwfl_symbol_lookup.insert(std::make_pair<DwflSymbolKey, SymbolIdx_t>(
        std::move(key), SymbolIdx_t(*symbol_idx)));
  }
#ifdef DEBUG
  printf("DBG: Func = %s  \n", table[*symbol_idx]._demangle_name.c_str());
  printf("     src  = %s  \n", table[*symbol_idx]._srcpath.c_str());
#endif
}

bool symbol_lookup_check(struct Dwfl_Module *mod, Dwarf_Addr newpc,
                         const Symbol &symbol) {

  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(mod, newpc, &loffset, &lsym,
                                                  &lshndxp, &lelfp, &lbias);
  bool error_found = false;
  if (loffset != symbol._offset) {
    LG_ERR("Error from cache offset %ld vs %ld ", loffset, symbol._offset);
    error_found = true;
  }
  if (localsymname == nullptr || symbol._symname.empty()) {
    if (localsymname != nullptr)
      LG_ERR("Error from cache : non null symname = %s", localsymname);
    if (!symbol._symname.empty())
      LG_ERR("Error from cache : non null cache entry = %s",
             symbol._symname.c_str());
  } else {
    if (strcmp(symbol._symname.c_str(), localsymname) != 0) {
      LG_ERR("Error from cache symname %s vs %s ", localsymname,
             symbol._symname.c_str());
      error_found = true;
    }
    if (error_found) {
      LG_ERR("symname = %s\n", symbol._symname.c_str());
    }
  }
  return error_found;
}

void DwflSymbolLookupStats::display() {
  if (_calls) {
    LG_NTC("symbol_lookup_stats : Hit / calls = [%d/%d] = %d", _hit, _calls,
           (_hit * 100) / _calls);
    LG_NTC("                   Errors / calls = [%d/%d] = %d", _errors, _calls,
           (_errors * 100) / _calls);
    // Estimate of cache size
    LG_NTC("                   Size of cache = %lu (nb el %d)",
           (_calls - _hit) * (sizeof(Symbol) + sizeof(DwflSymbolKey)),
           _calls - _hit);
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
