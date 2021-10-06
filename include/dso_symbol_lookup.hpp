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
  SymbolIdx_t get_or_insert_unhandled_type(const Dso &dso,
                                           SymbolTable &symbol_table);
  // map of maps --> allows to clear elements associated to DSO
  // creating one elt per address could represent a lot of mem : to be
  // monitored
  typedef std::unordered_map<ElfAddress_t, SymbolIdx_t> AddrDwflSymbolLookup;
  typedef std::unordered_map<DsoUID_t, AddrDwflSymbolLookup>
      DsoDwflSymbolLookup;
  DsoDwflSymbolLookup _map_dso;
  // For non-standard DSO types, address is not relevant
  std::unordered_map<dso::DsoType, SymbolIdx_t> _map_unhandled_dso;
};

} // namespace ddprof
