// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "build_id.hpp"

namespace ddprof {

namespace {
// convert integer to hex digit
inline char int_to_hex_digit(int c) {
  constexpr int k_a_hex_value = 0xa;
  return c < k_a_hex_value ? '0' + c : 'a' + (c - k_a_hex_value);
}
} // namespace

BuildIdStr format_build_id(BuildIdSpan build_id_span) {
  std::string build_id_str;
  build_id_str.resize(build_id_span.size() * 2);
  constexpr unsigned char hex_digit_bits = 4;
  constexpr unsigned char hex_digit_mask = (1 << hex_digit_bits) - 1;
  for (int i = 0; auto c : build_id_span) {
    build_id_str[i++] = int_to_hex_digit(c >> hex_digit_bits);
    build_id_str[i++] = int_to_hex_digit(c & hex_digit_mask);
  }
  return build_id_str;
}

} // namespace ddprof
