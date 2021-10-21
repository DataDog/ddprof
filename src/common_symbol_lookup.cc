#include "common_symbol_lookup.hpp"

namespace ddprof {

Symbol symbol_from_common(CommonSymbolLookup::LookupCases lookup_case) {
  switch (lookup_case) {
  case CommonSymbolLookup::LookupCases::truncated_stack:
    return Symbol(0, std::string(), std::string("[truncated]"), 0,
                  std::string());
  case CommonSymbolLookup::LookupCases::unknown_dso:
    return Symbol(0, std::string(), std::string("[unknown_dso]"), 0,
                  std::string());
  default:
    break;
  }
  return Symbol();
}

SymbolIdx_t
CommonSymbolLookup::get_or_insert(CommonSymbolLookup::LookupCases lookup_case,
                                  SymbolTable &symbol_table) {
  auto const it = _map.find(lookup_case);
  SymbolIdx_t symbol_idx;
  if (it != _map.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_common(lookup_case));
    _map.insert(std::pair<CommonSymbolLookup::LookupCases, SymbolIdx_t>(
        lookup_case, symbol_idx));
  }
  return symbol_idx;
}
} // namespace ddprof