// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.h"
#include "hash_helper.hpp"
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
  std::unordered_map<LookupCases, MapInfoIdx_t, EnumClassHash> _map;
};

} // namespace ddprof
