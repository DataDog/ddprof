// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdint>
#include <random>
#include <string>

namespace ddprof {
// The "xoshiro256** 1.0" generator.
// C++ port by Arthur O'Dwyer (2021).
// https://quuxplusone.github.io/blog/2021/11/23/xoshiro/ Based on the C version
// by David Blackman and Sebastiano Vigna (2018),
// https://prng.di.unimi.it/xoshiro256starstar.c

// NOLINTBEGIN(readability-magic-numbers)
class xoshiro256ss {
  uint64_t s[4]{};

  static constexpr uint64_t splitmix64(uint64_t &x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15UL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9UL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebUL;
    return z ^ (z >> 31);
  }

  static constexpr uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
  }

public:
  constexpr explicit xoshiro256ss() : xoshiro256ss(0) {}

  constexpr explicit xoshiro256ss(uint64_t seed) {
    s[0] = splitmix64(seed);
    s[1] = splitmix64(seed);
    s[2] = splitmix64(seed);
    s[3] = splitmix64(seed);
  }

  using result_type = uint64_t;
  static constexpr uint64_t min() { return 0; }
  static constexpr uint64_t max() { return static_cast<uint64_t>(-1); }

  constexpr uint64_t operator()() {
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
  }
};
// NOLINTEND(readability-magic-numbers)

inline constexpr char charset[] = "0123456789"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz";

template <typename TRandomGenerator>
std::string generate_random_string(TRandomGenerator &engine,
                                   std::size_t length) {
  // NOLINTNEXTLINE(misc-const-correctness)
  std::uniform_int_distribution<std::size_t> distribution(0,
                                                          sizeof(charset) - 2);
  std::string result;
  result.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    result.push_back(charset[distribution(engine)]);
  }
  return result;
}

} // namespace ddprof
