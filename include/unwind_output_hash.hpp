// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_output.hpp"

#include "hash_helper.hpp"

namespace ddprof {

struct UnwindOutputHash {
  std::size_t operator()(const UnwindOutput &uo) const noexcept {
    std::size_t seed = 0;
    hash_combine(seed, uo.pid);
    hash_combine(seed, uo.tid);
    for (const auto &fl : uo.locs) {
      hash_combine(seed, fl.ip);
      // no need to hash fl.elf_addr since it's derived from fl.ip
      hash_combine(seed, fl._symbol_idx);
      hash_combine(seed, fl._map_info_idx);
    }
    return seed;
  }
};

} // namespace ddprof