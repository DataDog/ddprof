// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbol_map.hpp"
#include <cassert>
#include <limits>

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

// parent span acts as a bound
NestedSymbolMap::FindRes
NestedSymbolMap::find_parent(NestedSymbolMap::ConstIt it,
                             const NestedSymbolKey &parent_bound,
                             Offset_t norm_pc) const {
  while (it != begin()) {
    --it;
    if (it->first < parent_bound) {
      return {end(), false};
    }
    if (is_within(norm_pc, *it)) {
      return {it, true};
    }
  }
  return {end(), false};
}

NestedSymbolMap::FindRes
NestedSymbolMap::find_closest(Offset_t norm_pc,
                              const NestedSymbolKey &parent_bound) const {
  // Use the element with the lowest end possible, to ensure we find the
  // deepest element
  auto it = lower_bound(NestedSymbolKey{norm_pc, 0});
  if (it != end()) { // map not empty
    if (is_within(norm_pc, *it)) {
      return {it, true};
    }
  }
  return find_parent(it, parent_bound, norm_pc);
}

bool NestedSymbolMap::is_within(const Offset_t &norm_pc,
                                const NestedSymbolMap::ValueType &kv) {
  if (norm_pc < kv.first.start) {
    return false;
  }
  if (norm_pc > kv.first.end) {
    return false;
  }
  return true;
}

} // namespace ddprof
