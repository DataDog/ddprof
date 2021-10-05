#pragma once

#include "dso.hpp"
#include "symbol_table.hpp"

#include <unordered_map>

namespace ddprof {

class DsoSymbolLookup {
public:
  SymbolIdx_t get_or_insert(ElfAddress_t addr, const Dso &dso,
                            SymbolTable &symbol_table);

  void clear_dso_symbols(DsoUID_t dso_id);

private:
  // map of maps --> allows to clear elements associated to DSO
  // creating one elt per address could represent a lot of mem : to be
  // monitored
  typedef std::unordered_map<ElfAddress_t, SymbolIdx_t> AddrDwflSymbolLookup;
  typedef std::unordered_map<DsoUID_t, AddrDwflSymbolLookup>
      DsoDwflSymbolLookup;
  DsoDwflSymbolLookup _map_dso;
};

} // namespace ddprof
