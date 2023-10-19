// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "ddres_def.hpp"

#include <chrono>
#include <string>

#ifdef __x86_64__
#  include <x86intrin.h>
#endif

namespace ddprof {

#ifdef __x86_64__
inline uint64_t read_tsc() { return __rdtsc(); }
#elif defined(__aarch64__)
inline uint64_t read_tsc() {
  uint64_t val;

  asm volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}
#else
inline uint64_t read_tsc() { return 0; }
#endif

struct TscClock {
public:
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<TscClock>;
  static constexpr bool is_steady = true;

  using Cycles = uint64_t;

  enum class State { kUninitialized, kUnavailable, kOK };
  enum class CalibrationMethod { kAuto, kPerf, kCpuArch, kClockMonotonicRaw };

  struct CalibrationParams {
    time_point offset;
    uint32_t mult;
    uint16_t shift;
  };

  struct Calibration {
    CalibrationParams params;
    State state;
    CalibrationMethod method;
  };

  static DDRes init(CalibrationMethod method = CalibrationMethod::kAuto);
  static CalibrationMethod get_calibration_method() noexcept {
    return _calibration.method;
  }

  static Cycles cycles_now() noexcept { return read_tsc(); }
  static time_point now() noexcept {
    return cycles_to_time_point(cycles_now());
  }
  static State state() noexcept { return _calibration.state; }
  static const Calibration &calibration() noexcept { return _calibration; }

  static duration cycles_to_duration(Cycles cycles) noexcept {
    using uint128_t = unsigned __int128;
    return duration{
        (static_cast<uint128_t>(cycles) * _calibration.params.mult) >>
        _calibration.params.shift};
  }

  static time_point cycles_to_time_point(Cycles cycles) noexcept {
    return _calibration.params.offset + cycles_to_duration(cycles);
  }

private:
  static bool init_from_perf(Calibration &calibration);

  // NOLINTNEXTLINE(cert-err58-cpp)
  inline static Calibration _calibration{
      {time_point{}, 1U, 0U}, State::kUninitialized, CalibrationMethod::kAuto};
};

inline std::string to_string(TscClock::CalibrationMethod method) {
  switch (method) {
  case TscClock::CalibrationMethod::kClockMonotonicRaw:
    return "ClockMonotonicRaw";
  case TscClock::CalibrationMethod::kCpuArch:
    return "CpuArch";
  case TscClock::CalibrationMethod::kPerf:
    return "perf";
  case TscClock::CalibrationMethod::kAuto:
    return "Auto";
  default:
    break;
  }

  return "undef";
}

} // namespace ddprof
