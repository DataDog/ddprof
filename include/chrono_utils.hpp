#pragma once

#include <chrono>
#include <sys/time.h>
#include <time.h>

namespace ddprof {
template <class T> struct is_duration : std::false_type {};

template <class Rep, class Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <class T> inline constexpr bool is_duration_v = is_duration<T>::value;

template <typename Duration>
requires is_duration_v<Duration>
constexpr timespec duration_to_timespec(Duration d) {
  auto nsecs = std::chrono::duration_cast<std::chrono::seconds>(d);
  d -= nsecs;

  return timespec{nsecs.count(), std::chrono::nanoseconds(d).count()};
}

template <typename Duration>
requires is_duration_v<Duration>
constexpr timeval duration_to_timeval(Duration d) {
  auto nsecs = std::chrono::duration_cast<std::chrono::seconds>(d);
  d -= nsecs;

  return timeval{
      nsecs.count(),
      std::chrono::duration_cast<std::chrono::microseconds>(d).count()};
}

constexpr std::chrono::nanoseconds timespec_to_duration(timespec ts) {
  return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

constexpr std::chrono::microseconds timeval_to_duration(timeval tv) {
  return std::chrono::seconds{tv.tv_sec} +
      std::chrono::microseconds{tv.tv_usec};
}

} // namespace ddprof