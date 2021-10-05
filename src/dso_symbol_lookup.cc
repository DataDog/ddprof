#include "dso_symbol_lookup.hpp"

#include "dso_type.hpp"
#include "string_format.hpp"

namespace ddprof {

namespace {

Symbol symbol_from_dso(ElfAddress_t addr, const Dso &dso) {
  // address that means something for our user (addr)
  Offset_t normalized_addr = (addr - dso._start) + dso._pgoff;
  std::string dso_dbg_str = string_format("[%p:dso]", normalized_addr);
  return Symbol(dso._pgoff, std::string(), dso_dbg_str, 0,
                dso.format_filename());
}
} // namespace

SymbolIdx_t DsoSymbolLookup::get_or_insert(ElfAddress_t addr, const Dso &dso,
                                           SymbolTable &symbol_table) {
  AddrDwflSymbolLookup &addr_lookup = _map_dso[dso._id];
  auto const it = addr_lookup.find(addr);
  SymbolIdx_t symbol_idx;
  if (it != addr_lookup.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_dso(addr, dso));
    addr_lookup.insert(std::pair<ElfAddress_t, SymbolIdx_t>(addr, symbol_idx));
  }
  return symbol_idx;
}

void DsoSymbolLookup::clear_dso_symbols(DsoUID_t dso_id) {
  _map_dso.erase(dso_id);
}
} // namespace ddprof
