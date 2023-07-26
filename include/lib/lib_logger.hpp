#pragma once

#include <mutex>
#include <cstdarg>
#include <cstdio>

namespace ddprof {
template<typename... Args>
void log_once(const char* format, Args... args) {
  static std::once_flag flag;
  std::call_once(flag, [&, format]() {
    fprintf(stderr, format, args...);
  });
}
}
