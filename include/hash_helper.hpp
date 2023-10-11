// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>
#include <functional>

namespace ddprof {

template <class T>
inline constexpr void hash_combine(std::size_t &seed, const T &v) {
  // NOLINTNEXTLINE(readability-magic-numbers)
  seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace ddprof
