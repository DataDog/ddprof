// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

namespace ddprof {
using Buffer = std::span<std::byte>;
using ConstBuffer = std::span<const std::byte>;

template <typename T>
std::span<T> remaining(std::span<T> buffer, size_t offset) {
  offset = std::min(offset, buffer.size());
  return {buffer.data() + offset, buffer.size() - offset};
}
} // namespace ddprof
