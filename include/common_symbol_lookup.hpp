#pragma once

#include "symbol_table.hpp"

#include <unordered_map>

namespace ddprof {
// Generates virtual frames for common unhandled cases
class CommonSymbolLookup {
public:
  enum LookupCases {
    truncated_stack,
    unknown_dso,
  };
  SymbolIdx_t get_or_insert(LookupCases lookup_case, SymbolTable &symbol_table);

private:
  std::unordered_map<LookupCases, SymbolIdx_t> _map;
};

} // namespace ddprof
