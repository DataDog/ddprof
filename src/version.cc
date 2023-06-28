// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "version.hpp"

#include <stdio.h>

std::string_view str_version() {
  static char profiler_version[1024] = {0};
  int len = 0;
  if (*VER_REV)
    len = snprintf(profiler_version, 1024, "%d.%d.%d+%s", VER_MAJ, VER_MIN,
                   VER_PATCH, VER_REV);
  else
    len = snprintf(profiler_version, 1024, "%d.%d.%d", VER_MAJ, VER_MIN,
                   VER_PATCH);

  if (len < 0) {
    return "bad-version";
  }
  return std::string_view{profiler_version, static_cast<unsigned>(len)};
}

void print_version() {
  printf(MYNAME " %.*s\n", static_cast<int>(str_version().size()),
         str_version().data());
}
