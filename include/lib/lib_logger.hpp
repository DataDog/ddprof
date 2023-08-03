// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace ddprof {
template <typename... Args>
void log_once(char const *const format, Args... args) {
#ifndef DEBUG
  static std::once_flag flag;
  std::call_once(flag, [&, format]() { fprintf(stderr, format, args...); });
#else
  fprintf(stderr, format, args...);
#endif
}
} // namespace ddprof
