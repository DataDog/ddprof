// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "hash_helper.hpp"
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
  std::unordered_map<LookupCases, SymbolIdx_t, EnumClassHash> _map;
};

} // namespace ddprof
