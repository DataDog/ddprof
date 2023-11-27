// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#define DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION() __asm__ __volatile__("")
#define DDPROF_NOINLINE __attribute__((noinline))
#define DDPROF_ALWAYS_INLINE __attribute__((always_inline))
#define DDPROF_NO_SANITIZER_ADDRESS __attribute__((no_sanitize("address")))
#define DDPROF_WEAK __attribute__((weak))

#if defined(__clang__)
#  define DDPROF_NOIPO __attribute__((noinline))
#else
#  define DDPROF_NOIPO __attribute__((noipa))
#endif

// Taken from google::benchmark
namespace ddprof {
template <class Tp>
inline DDPROF_ALWAYS_INLINE void DoNotOptimize(Tp const &value) {
  asm volatile("" : : "r,m"(value) : "memory");
}

template <class Tp> inline DDPROF_ALWAYS_INLINE void DoNotOptimize(Tp &value) {
#ifndef __clang_analyzer__
#  if defined(__clang__)
  asm volatile("" : "+r,m"(value) : : "memory");
#  else
  asm volatile("" : "+m,r"(value) : : "memory");
#  endif
#endif
}
} // namespace ddprof