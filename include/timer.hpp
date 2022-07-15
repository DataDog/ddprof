// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "ddres_def.hpp"

#include <chrono>

#ifdef __x86_64__
#  include <x86intrin.h>
#endif

namespace ddprof {

enum class TscState { kUninitialized, kUnavailable, kOK };

struct TscConversion {
  uint16_t shift;
  uint32_t mult;
  TscState state;
};

inline TscConversion g_tsc_conversion = {0, 1UL, TscState::kUninitialized};

using TscCycles = uint64_t;

#ifdef __x86_64__
inline TscCycles read_tsc() { return __rdtsc(); }
#elif defined(__aarch64__)
inline TscCycles read_tsc() {
  uint64_t val;

  asm volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}
#else
inline TscCycles read_tsc() { return 0; }
#endif

enum class TscCalibrationMethod { kAuto, kPerf, kCpuArch, kClockMonotonicRaw };

DDRes init_tsc(TscCalibrationMethod method = TscCalibrationMethod::kAuto);

inline TscState get_tsc_state() { return g_tsc_conversion.state; }

inline TscCycles get_tsc_cycles() { return read_tsc(); }

inline uint64_t tsc_cycles_to_ns(TscCycles cycles) {
  uint32_t al = cycles;
  uint32_t ah = cycles >> 32;

  uint16_t shift = g_tsc_conversion.shift;
  uint32_t b = g_tsc_conversion.mult;

  uint64_t ret = ((uint64_t)al * b) >> shift;
  if (ah)
    ret += ((uint64_t)ah * b) << (32 - shift);

  return ret;
}

inline std::chrono::nanoseconds tsc_cycles_to_duration(TscCycles cycles) {
  return std::chrono::nanoseconds{tsc_cycles_to_ns(cycles)};
}

} // namespace ddprof