// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "address_map.hpp"

namespace ddprof {

  template <typename VAL_T>
  bool AddressMap::insert(uint64_t lh_key, uint64_t rh_key, VAL_T val) {
    // ASSERT:  you can't have a function of length 1 in a perfmap.
    if (lh_key == rh_key)
      return false;
    // If the underlying map is empty, then we can just insert it straight away
    if (_ranges.empty()) {
      _ranges.insert(lh_key, val);
      _ranges.insert(rh_key, val);
      return true;
    }

    // UB(x) = _ranges.upper_bound(x).first
    // [] - existing ranges
    // () - this range
    // [1](A)[2]  --- good, UB(lh) == UB(rh)
    // [1(A][2])[3] -- A intersects 1,2
    // (A[1])[2] -- A intersects 1
    // [1(A)] -- A intersects 1 and UB(lh) == UB(rh)
    // (A)[1] -- good, but UB(lh) == UB(rh) == end()
    // [1](A) -- good, but UB(lh( == UB(rh)
    auto lh_it = _ranges.upper_bound(lh_key);
    auto rh_it = _ranges.upper_bound(rh_key);


  }
}
