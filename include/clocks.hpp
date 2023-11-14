// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "chrono_utils.hpp"

#include <chrono>
#include <cstdint>
#include <time.h>

namespace ddprof {

struct ThreadCpuClock {
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<ThreadCpuClock, duration>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept {
    timespec tp;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
    return time_point(timespec_to_duration(tp));
  }
};

struct CoarseMonotonicClock {
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<ThreadCpuClock, duration>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept {
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
    return time_point(timespec_to_duration(tp));
  }
};

} // namespace ddprof
