// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>

namespace ddprof {

static inline std::size_t hash_combine(std::size_t lhs, std::size_t rhs) {
  return rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
}

// Prior to c++ 14, this is required for enums
struct EnumClassHash {
  template <typename T> std::size_t operator()(T t) const {
    return static_cast<std::size_t>(t);
  }
};

} // namespace ddprof
