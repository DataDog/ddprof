// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ddprof {
struct Uuid {
  Uuid();
  static constexpr int k_version_position = 12;
  static constexpr int k_variant_position = 16;
  static constexpr int k_version = 4;
  [[nodiscard]] int get_version() const { return data[k_version_position]; }
  [[nodiscard]] std::string to_string() const;
  // We could make this smaller, as it is hexadecimal and 32 characters
  // but we are keeping one byte per element for simplicity
  std::array<uint8_t, 32> data;
};
} // namespace ddprof
