// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_output.hpp"

namespace ddprof {

template <class T> inline void hash_combine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct UnwindOutputHash {
  std::size_t operator()(const UnwindOutput_V2 &uo) const noexcept {
    std::size_t seed = 0;
    hash_combine(seed, uo.pid);
    hash_combine(seed, uo.tid);
    for (unsigned i = 0; i<uo.nb_locs; ++i) {
      // ip should be enough to guarantee unicity
      hash_combine(seed, uo.callchain[i]);
    }
    return seed;
  }
};

} // namespace ddprof