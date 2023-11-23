// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "version.hpp"

#include <absl/strings/substitute.h>
#include <cstdio>

namespace ddprof {

std::string_view str_version() {
  static std::string const version_str = *VER_REV
      ? absl::Substitute("$0.$1.$2+$3", VER_MAJ, VER_MIN, VER_PATCH, VER_REV)
      : absl::Substitute("$0.$1.$2", VER_MAJ, VER_MIN, VER_PATCH);

  return std::string_view{version_str};
}

void print_version() {
  printf(MYNAME " %.*s\n", static_cast<int>(str_version().size()),
         str_version().data());
}

} // namespace ddprof
