// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <chrono>
#include <cstdint>
#include <time.h>

namespace ddprof {

enum class PerfClockSource : uint8_t {
  kClockMonotonic = CLOCK_MONOTONIC,
  kClockMonotonicRaw = CLOCK_MONOTONIC_RAW,
  kMaxPosixClock = 64,
  kTSC = 65,
  kNoClock = 255
};

// PerfClock is meant to be a clock that has the same timesource as perf
// PerfClock::determine_perf_clock_source should be called first to determine
// which clock source can work with perf.
struct PerfClock {
public:
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<PerfClock>;
  static constexpr bool is_steady = true;

  static time_point now() noexcept { return time_point{_clock_func()}; }

  // Determine which perf clock source to use
  static PerfClockSource init() noexcept;

  static void init(PerfClockSource clock_source) noexcept;

  static PerfClockSource perf_clock_source() { return _clock_source; }

  // Reset PerfClock to its initial state (just return 0)
  static void reset() {
    _clock_source = PerfClockSource::kNoClock;
    _clock_func = null_clock;
  }

private:
  using PerfClockFunc = std::chrono::nanoseconds (*)();
  static duration null_clock() { return duration{}; }
  inline static PerfClockFunc _clock_func{null_clock};
  inline static PerfClockSource _clock_source{PerfClockSource::kNoClock};
};

inline PerfClock::time_point
perf_clock_time_point_from_timestamp(uint64_t timestamp) {
  return PerfClock::time_point{PerfClock::duration{timestamp}};
}

} // namespace ddprof
