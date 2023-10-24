// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.hpp"

#include <cassert>
#include <strings.h>

namespace ddprof {

int arg_which(const char *str, char const *const *set, int sz_set) {
  if (!str || !set) {
    return -1;
  }
  for (int i = 0; i < sz_set; i++) {
    if (set[i] && !strcasecmp(str, set[i])) {
      return i;
    }
  }
  return -1;
}

bool arg_inset(const char *str, char const *const *set, int sz_set) {
  return !(-1 == arg_which(str, set, sz_set));
}

bool arg_yesno(const char *str, int mode) {
  const int sizeOfPatterns = 3;
  static const char *yes_set[] = {"yes", "true", "on"}; // mode = 1
  static const char *no_set[] = {"no", "false", "off"}; // mode = 0
  assert(0 == mode || 1 == mode);
  char const *const *set = (!mode) ? no_set : yes_set;
  return arg_which(str, set, sizeOfPatterns) != -1;
}

} // namespace ddprof
