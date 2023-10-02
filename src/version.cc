// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "version.hpp"

#include <cstdio>

namespace ddprof {

std::string_view str_version() {
  constexpr size_t k_max_version_length = 1024;
  static char profiler_version[k_max_version_length] = {};
  int len = 0;
  if (*VER_REV) {
    len = snprintf(profiler_version, std::size(profiler_version), "%d.%d.%d+%s",
                   VER_MAJ, VER_MIN, VER_PATCH, VER_REV);
  } else {
    len = snprintf(profiler_version, std::size(profiler_version), "%d.%d.%d",
                   VER_MAJ, VER_MIN, VER_PATCH);
  }

  if (len < 0) {
    return "bad-version";
  }
  return std::string_view{profiler_version, static_cast<unsigned>(len)};
}

void print_version() {
  printf(MYNAME " %.*s\n", static_cast<int>(str_version().size()),
         str_version().data());
}

} // namespace ddprof
