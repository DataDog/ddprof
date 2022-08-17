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
  alignas(hardware_destructive_interference_size) std::atomic<bool> spinlock;
};

inline bool lock(std::atomic<bool> *lock, std::chrono::milliseconds timeout) {
  // Taken from
  // https://probablydance.com/2019/12/30/measuring-mutexes-spinlocks-and-how-bad-the-linux-scheduler-really-is/
  static constexpr uint32_t k_max_active_spin = 4000;
  static constexpr std::chrono::nanoseconds k_yield_sleep =
      std::chrono::microseconds(500);

  uint32_t spincount = 0;
  std::chrono::steady_clock::time_point deadline{};

  for (;;) {
    if (!lock->exchange(true, std::memory_order_acquire)) {
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
        if (deadline.time_since_epoch().count() == 0) {
          deadline = std::chrono::steady_clock::now() + timeout;
        } else {
          if (std::chrono::steady_clock::now() > deadline) {
            // timeout
            return false;
          }
        }
        // If active spin fails, yield
        std::this_thread::sleep_for(k_yield_sleep);
      }
      // Wait for lock to be released without generating cache misses
    } while (lock->load(std::memory_order_relaxed));
  }
  return true;
}

inline void unlock(std::atomic<bool> *lock) {
  lock->store(false, std::memory_order_release);
}
} // namespace ddprof
