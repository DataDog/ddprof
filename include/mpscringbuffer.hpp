// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "ddprof_base.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace ddprof {
//  mimic: std::hardware_destructive_interference_size, C++17
inline constexpr std::size_t hardware_destructive_interference_size = 128;

class SpinLock {
public:
  void lock() {
    try_lock_until_slow(std::chrono::steady_clock::time_point::max());
  }

  template <typename Rep, typename Period>
  bool try_lock_for(std::chrono::duration<Rep, Period> timeout_duration) {
    return lock_fast() ? true
                       : try_lock_until_slow(std::chrono::steady_clock::now() +
                                             timeout_duration);
  }

  void unlock() { _flag.store(false, std::memory_order_release); }

private:
  static inline constexpr uint32_t k_max_active_spin = 4000;
  static inline constexpr std::chrono::nanoseconds k_yield_sleep =
      std::chrono::microseconds(500);

  bool lock_fast() {
    // Taken from
    // https://probablydance.com/2019/12/30/measuring-mutexes-spinlocks-and-how-bad-the-linux-scheduler-really-is/
    uint32_t spincount = 0;

    for (;;) {
      if (!_flag.exchange(true, std::memory_order_acquire)) {
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
          return false;
        }
        // Wait for lock to be released without generating cache misses
      } while (_flag.load(std::memory_order_relaxed));
    }
    return true;
  }

  DDPROF_NOINLINE bool
  try_lock_until_slow(std::chrono::steady_clock::time_point timeout_time) {
    for (;;) {
      if (!_flag.exchange(true, std::memory_order_acquire)) {
        break;
      }
      do {
        if (std::chrono::steady_clock::now() > timeout_time) {
          // timeout
          return false;
        }
        // If active spin fails, yield
        std::this_thread::sleep_for(k_yield_sleep);

      } while (_flag.load(std::memory_order_relaxed));
    }
    return true;
  }

  std::atomic_bool _flag{};
};

struct MPSCRingBufferMetaDataPage {
  alignas(hardware_destructive_interference_size) uint64_t writer_pos;
  alignas(hardware_destructive_interference_size) uint64_t reader_pos;
  alignas(hardware_destructive_interference_size) SpinLock spinlock;
};

} // namespace ddprof
