// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace ddprof {
//  mimic: std::hardware_destructive_interference_size, C++17
constexpr std::size_t hardware_destructive_interference_size = 128;

struct MPSCRingBufferMetaDataPage {
  alignas(hardware_destructive_interference_size) uint64_t writer_pos;
  alignas(hardware_destructive_interference_size) uint64_t reader_pos;
  alignas(hardware_destructive_interference_size) std::atomic_flag spinlock;
};

inline void lock(std::atomic_flag *lock) {
  static constexpr uint32_t k_max_active_spin = 4000;
  static constexpr std::chrono::nanoseconds k_yield_sleep =
      std::chrono::microseconds(500);

  uint32_t spincount = 0;

  for (;;) {
    if (!lock->test_and_set(std::memory_order_acquire)) {
      break;
    }
    do {
      if (spincount < k_max_active_spin) {
        ++spincount;
#ifdef __x86_64__
        asm volatile("pause");
#else
        asm volatile("yield");
#endif
      } else {
        // If active spin fails, yield
        std::this_thread::sleep_for(k_yield_sleep);
      }
      // Wait for lock to be released without generating cache misses
    } while (lock->test(std::memory_order_relaxed));
  }
}

inline void unlock(std::atomic_flag *lock) {
  lock->clear(std::memory_order_release);
}
} // namespace ddprof
