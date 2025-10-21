// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <stdint.h>

namespace ddprof {

// Stateless deterministic address sampling
// Decides whether to track an address based solely on its value
// - Zero memory overhead (no data structure)
// - Zero contention (no shared state)
// - Deterministic (same address always gets same decision)
// - Signal-safe (pure computation)
class AddressSampler {
public:
  // Sampling rates: 1 in N addresses will be tracked
  enum class SamplingRate {
    EVERY_1 = 0,     // Track all (mask = 0b0)
    EVERY_2 = 1,     // Track 1/2 (mask = 0b1)
    EVERY_4 = 3,     // Track 1/4 (mask = 0b11)
    EVERY_8 = 7,     // Track 1/8 (mask = 0b111)
    EVERY_16 = 15,   // Track 1/16
    EVERY_32 = 31,   // Track 1/32
    EVERY_64 = 63,   // Track 1/64
    EVERY_128 = 127, // Track 1/128
    EVERY_256 = 255, // Track 1/256
    EVERY_512 = 511, // Track 1/512
  };

  explicit AddressSampler(SamplingRate rate = SamplingRate::EVERY_1)
      : _sampling_mask(static_cast<uint32_t>(rate)) {}

  // Deterministically decide if this address should be tracked
  // Same address always returns same result (across all threads, all time)
  [[nodiscard]] bool should_track(uintptr_t addr) const {
    if (_sampling_mask == 0) {
      return true; // Track everything
    }

    uint32_t hash = hash_address(addr);
    return (hash & _sampling_mask) == 0;
  }

  // Alignment-aware sampling: bias toward page-aligned allocations
  // Page-aligned addresses are often large allocations (mmap, big malloc)
  // This works at both malloc() and free() time (only needs address!)
  [[nodiscard]] bool should_track_alignment_aware(uintptr_t addr) const {
    // Always track page-aligned addresses (likely large allocations)
    // Page-aligned = lower 12 bits are zero
    constexpr uintptr_t kPageMask = 0xFFF;
    if ((addr & kPageMask) == 0) {
      return true; // 100% track page-aligned
    }

    // For non-page-aligned, use normal sampling
    if (_sampling_mask == 0) {
      return true;
    }

    uint32_t hash = hash_address(addr);
    return (hash & _sampling_mask) == 0;
  }

  // Multi-tier alignment-aware sampling
  // Higher alignment → more likely large allocation → higher sample rate
  [[nodiscard]] bool should_track_with_alignment_bias(uintptr_t addr) const {
    if (_sampling_mask == 0) {
      return true;
    }

    // Check alignment (count trailing zeros)
    // Page-aligned (4KB): 12 bits
    // 64-byte aligned: 6 bits
    // 16-byte aligned: 4 bits
    int alignment_bits = __builtin_ctzl(addr | 1); // |1 prevents inf loop on 0

    uint32_t hash = hash_address(addr);

    // Adjust sampling rate based on alignment
    // Higher alignment → smaller mask → more likely to track
    if (alignment_bits >= 12) {
      // Page-aligned: always track
      return true;
    } else if (alignment_bits >= 10) {
      // 1KB-aligned: 4× more likely
      uint32_t adjusted_mask = _sampling_mask >> 2;
      return (hash & adjusted_mask) == 0;
    } else if (alignment_bits >= 8) {
      // 256-byte aligned: 2× more likely
      uint32_t adjusted_mask = _sampling_mask >> 1;
      return (hash & adjusted_mask) == 0;
    } else {
      // Small alignment: normal sampling
      return (hash & _sampling_mask) == 0;
    }
  }

  // Get current sampling rate (for diagnostics)
  [[nodiscard]] uint32_t get_sampling_rate() const {
    return _sampling_mask + 1;
  }

private:
  uint32_t _sampling_mask;

  // High-quality hash function for address sampling
  // Must have good avalanche properties so sequential addresses
  // are uniformly distributed in sampling decision
  [[nodiscard]] static uint32_t hash_address(uintptr_t addr) {
    // Remove lower alignment bits (always 0 for aligned allocations)
    uint64_t h = addr >> 4;

    // MurmurHash3-style mixing for good avalanche
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= h >> 32;
    h *= 0x85EBCA77C2B2AE63ULL;
    h ^= h >> 32;

    return static_cast<uint32_t>(h);
  }
};

} // namespace ddprof
