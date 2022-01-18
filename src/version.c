// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "version.h"
#include "string_view.h"

#include <stdio.h>

string_view str_version() {
  static char profiler_version[1024] = {0};
  string_view version_str;
  if (*VER_REV)
    version_str.len = snprintf(profiler_version, 1024, "%d.%d.%d+%s", VER_MAJ,
                               VER_MIN, VER_PATCH, VER_REV);
  else
    version_str.len = snprintf(profiler_version, 1024, "%d.%d.%d", VER_MAJ,
                               VER_MIN, VER_PATCH);

  version_str.ptr = profiler_version;

  return version_str;
}

void print_version() { printf(MYNAME " %s\n", str_version().ptr); }
