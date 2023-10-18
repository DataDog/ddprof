// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "system_checks.hpp"

#include "chrono_utils.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "tsc_clock.hpp"
#include "unique_fd.hpp"

#include <chrono>
#include <fstream>
#include <optional>
#include <sys/resource.h>
#include <unistd.h>

namespace ddprof {

namespace {

constexpr auto kCurrentClockSourceSysFsPath =
    "/sys/devices/system/clocksource/clocksource0/current_clocksource";
constexpr auto kAvailableClockSourcesSysFsPath =
    "/sys/devices/system/clocksource/clocksource0/available_clocksource";

std::optional<std::string> read_line_from_file(const char *path) {
  std::ifstream ifs(path);
  std::string line;
  std::getline(ifs, line);
  return ifs ? std::optional{line} : std::nullopt;
}

void check_clock_source() {
  auto line = read_line_from_file(kCurrentClockSourceSysFsPath);
  if (line == "xen") {
    LG_WRN("xen clock source detected. This might lead to degraded "
           "performance.");
  }
}

void check_clock_vdso() {
  // This function continuously calls std::chrono::steady_clock::now during 5
  // kernel ticks and then check with getrusage that system cpu time consumed
  // is less than 10% of total cpu time consumed during this period.
  // If not, it implies that significant time was spent in the kernel, and
  // therefore that either the call to clock_gettime is not vdso accelerated
  // or that the vsdo function falls back to the kernel (eg. this happens if
  // clock source is xen).
  constexpr auto kMeasureDurationInClocks = 5;
  constexpr auto kMaxSystemTimePercentage = 10;

  auto kernel_clocks_per_sec = sysconf(_SC_CLK_TCK);

  auto measure_duration = std::chrono::nanoseconds{std::chrono::seconds{1}} *
      kMeasureDurationInClocks / kernel_clocks_per_sec;

  rusage ru_before;
  if (getrusage(RUSAGE_SELF, &ru_before) != 0) {
    return;
  }
  auto deadline = std::chrono::steady_clock::now() + measure_duration;
  while (std::chrono::steady_clock::now() < deadline) {}
  rusage ru_after;
  if (getrusage(RUSAGE_SELF, &ru_after) != 0) {
    return;
  }
  auto user_time = timeval_to_duration(ru_after.ru_utime) -
      timeval_to_duration(ru_before.ru_utime);
  auto system_time = timeval_to_duration(ru_after.ru_stime) -
      timeval_to_duration(ru_before.ru_stime);

  if (system_time >= (user_time + system_time) * kMaxSystemTimePercentage /
          100) { // NOLINT(readability-magic-numbers)
    auto current_clock =
        read_line_from_file(kCurrentClockSourceSysFsPath).value_or("unknown");
    auto available_clocks = read_line_from_file(kAvailableClockSourcesSysFsPath)
                                .value_or("unknown");
    LG_WRN("Slow clock source detected. Current clock source: %s. Available "
           "clock sources: %s.",
           current_clock.c_str(), available_clocks.c_str());
  }
}
} // namespace

DDRes run_system_checks() {
  check_clock_source();
  check_clock_vdso();
  return {};
}
} // namespace ddprof
