// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbol_map.hpp"

namespace ddprof {

bool SymbolMap::is_within(const Offset_t &norm_pc,
                          const SymbolMap::ValueType &kv) {
  if (norm_pc < kv.first) {
    return false;
  }
  if (norm_pc > kv.second.get_end()) {
    return false;
  }
  return true;
}

SymbolMap::FindRes SymbolMap::find_closest(Offset_t norm_pc) {
  // First element not less than (can match exactly a start addr)
  auto it = lower_bound(norm_pc);
  if (it != end()) { // map is empty
    if (SymbolMap::is_within(norm_pc, *it)) {
      return {it, true};
    }
  }

  // previous element is more likely to contain our addr
  if (it != begin()) {
    --it;
  } else { // map is empty
    return {end(), false};
  }
  // element can not be end (as we reversed or exit)
  return {it, is_within(norm_pc, *it)};
}


NestedSymbolMap::FindRes NestedSymbolMap::find_closest(Offset_t norm_pc) {
  auto it = lower_bound(norm_pc);
  if (it != end()) { // map is empty
    if (is_within(norm_pc, *it)) {
      return {it, true};
    }
  }
  if (it != begin()) {
    --it;
  }
  // Iteratively check the current symbol and its parent symbols
  while (it != end()) {
    if (is_within(norm_pc, *it)) {
      return {it, true};
    }
    // Traverse to the parent symbol if available
    if (it->second.get_parent_addr() != 0) {
      it = find(it->second.get_parent_addr());
    } else {
      break; // No parent, stop the search
    }
  }
  return {end(), false};
}

bool NestedSymbolMap::is_within(const Offset_t &norm_pc, const NestedSymbolMap::ValueType &kv) {
  if (norm_pc < kv.first) {
    return false;
  }
  if (norm_pc > kv.second.get_end()) {
    return false;
  }
  return true;
}

} // namespace ddprof
