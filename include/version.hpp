// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// Everything above the __cplusplus guard must be valid C — this header is
// included from loader.c.
// Name and versions are defined in build system
#ifndef MYNAME
#  define MYNAME "ddprof"
#endif

#ifndef VER_MAJ
#  define VER_MAJ 0
#endif
#ifndef VER_MIN
#  define VER_MIN 0
#endif
#ifndef VER_PATCH
#  define VER_PATCH 0
#endif
#ifndef VER_REV
#  define VER_REV "custom"
#endif

// Compile-time version string usable from both C and C++.
// Format: "MAJ.MIN.PATCH+REV"
#define DDPROF_STRINGIFY2_(x) #x
#define DDPROF_STRINGIFY_(x) DDPROF_STRINGIFY2_(x)
#define DDPROF_VERSION_STR                                                     \
  DDPROF_STRINGIFY_(VER_MAJ)                                                   \
  "." DDPROF_STRINGIFY_(VER_MIN) "." DDPROF_STRINGIFY_(VER_PATCH) "+" VER_REV

#ifdef __cplusplus
#  include <string_view>

namespace ddprof {
/// Versions are updated in cmake files
std::string_view str_version();

void print_version();

} // namespace ddprof
#endif // __cplusplus
