// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace ddprof {

#ifdef NDEBUG
template <typename Func>
void log_once_helper(std::once_flag &flag, Func &&func) {
  std::call_once(flag, std::forward<Func>(func));
#else
template <typename Func>
void log_once_helper(std::once_flag & /*unused*/, Func &&func) {
  std::forward<Func>(func)();
#endif
}

// create a once flag for the line and file where this is called:
#define LOG_ONCE(format, ...)                                                  \
  do {                                                                         \
    static std::once_flag UNIQUE_ONCE_FLAG_##__COUNTER__;                      \
    ddprof::log_once_helper(UNIQUE_ONCE_FLAG_##__COUNTER__, [&]() {            \
      fprintf(stderr, (format), ##__VA_ARGS__);                                \
    });                                                                        \
  } while (0)

} // namespace ddprof
