#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace ddprof {
template <typename... Args>
void log_once(char const *const format, Args... args) {
  static std::once_flag flag;
  std::call_once(flag, [&, format]() { fprintf(stderr, format, args...); });
}
} // namespace ddprof
