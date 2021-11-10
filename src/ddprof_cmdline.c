// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int arg_which(const char *str, char const *const *set, int sz_set) {
  if (!str || !set)
    return -1;
  for (int i = 0; i < sz_set; i++) {
    if (set[i] && !strcasecmp(str, set[i]))
      return i;
  }
  return -1;
}

bool arg_inset(const char *str, char const *const *set, int sz_set) {
  int ret = arg_which(str, set, sz_set);
  return !(-1 == ret);
}

bool arg_yesno(const char *str, int mode) {
  const int sizeOfPatterns = 3;
  static const char *yes_set[] = {"yes", "true", "on"}; // mode = 1
  static const char *no_set[] = {"no", "false", "off"}; // mode = 0
  assert(0 == mode || 1 == mode);
  char const *const *set = (!mode) ? no_set : yes_set;
  if (arg_which(str, set, sizeOfPatterns) != -1) {
    return true;
  }
  return false;
}

bool process_event(const char *str, const char **lookup, size_t sz_lookup,
                   size_t *idx, uint64_t *value) {
  size_t sz_str = strlen(str);

  for (size_t i = 0; i < sz_lookup; ++i) {
    size_t sz_key = strlen(lookup[i]);
    if (!strncmp(lookup[i], str, sz_key)) {
      // If the user didn't specify anything else, we're done.
      if (sz_str == sz_key) {
        *idx = i;
        return true;
      }

      // perf_event_open() expects unsigned 64-bit integers, but it's somewhat
      // annoying to process unsigned ints using the standard interface.  We
      // take what we can get and convert to unsigned via absolute value.
      uint64_t value_tmp = 0;
      char *str_tmp = (char *)&str[sz_key];
      char *str_chk = str_tmp;

      // We use a comma as a separator; if it doesn't immediately precede the
      // label, then any subsequent processing is invalid.
      if (*str_tmp != ',')
        return false;

      // Currently, we demand that the entire numeric portion of the event
      // specifier is valid.  This is the place to add code for suffix support,
      // probably :)
      value_tmp = strtoll(&str_tmp[1], &str_chk, 10);
      if (*str_chk)
        return false;

      // If we're here, we received a valid event and a valid numeric portion.
      *idx = i;
      *value = value_tmp;
      return true;
    }
  }

  return false;
}
