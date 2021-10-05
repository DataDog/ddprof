#pragma once

#include "ddprof_defs.h"
#include "mapinfo_table.hpp"

#include <string>
#include <unordered_map>

namespace ddprof {
// Generates virtual frames for common unhandled cases
class CommonMapInfoLookup {
public:
  enum LookupCases {
    empty, // when mapping info is not relevant, just put am empty field
  };

  SymbolIdx_t get_or_insert(LookupCases lookup_case,
                            MapInfoTable &mapinfo_table);

private:
  std::unordered_map<LookupCases, MapInfoIdx_t> _map;
};

} // namespace ddprof
