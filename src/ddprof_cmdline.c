// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.h"
#include "logger.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

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


#ifdef __aarch64__
#  warning tracepoints are broken on ARM
#endif
int arg2reg[] = {0, 5, 4, 3, 2, 16, 17};
uint8_t get_register(const char *str) {
  uint8_t reg = 0;
  char *str_copy = (char *)str;
  long reg_buf = strtol(str, &str_copy, 10);
  if (!*str_copy) {
    reg = reg_buf;
  } else {
    reg = 0;
    LG_NTC("Could not parse register %s", str);
  }

  // If we're here, then we have a register.
  return arg2reg[reg];
}

bool get_trace_format(const char *str, uint8_t *trace_off, uint8_t *trace_sz) {
  char *str_copy = (char *)str;
  if (!str)
    return false;

  char *period = strchr(str, '.');
  char *period_copy = period;
  if (!period)
    return false;

  *trace_off = strtol(str, &str_copy, 10);
  *trace_sz = strtol(period + 1, &period_copy, 10);

  // Error if the size is zero, otherwise fine probably
  return !trace_sz;
}

ptret_t process_tracepoint(const char *str, traceconfig_t *conf) {
  // minimum form; provides counts, samples every hit
  // -t groupname:tracename
  // Register-qualified form
  // -t groupname:tracename%REG
  // -t groupname:tracename$offset.size
  // Sample-qualified form, sets a period value
  // -t groupname:tracename@period
  // full
  // -t groupename:tracename%REG@period
  // groupname, tracename, REG - strings
  // REG - can be a number 1-6
  // period is a number
  size_t sz_str = strlen(str);
  char *groupname;
  char *tracename;
  uint8_t reg = 0;
  uint64_t period = 1;
  bool is_raw = false;
  uint8_t trace_off = 0;
  uint8_t trace_sz = 0;

  // Check format
  if (!sz_str)
    return PTRET_BADFORMAT;
  char *colon = strchr(str, ':');
  char *perc = strchr(str, '%');
  char *amp = strchr(str, '@');
  char *dollar = strchr(str, '$');

  if (!colon)
    return PTRET_BADFORMAT;
  if (dollar && perc)
    return PTRET_BADFORMAT;

  // Split strings
  if (colon)
    *colon = 0;
  if (perc)
    *perc = 0;
  if (amp)
    *amp = 0;
  if (dollar)
    *dollar = 0;

  // Name checking
  groupname = strdup(str);
  tracename = strdup(colon+1);

  // If a register is specified, process that
  if (perc)
    reg = get_register(perc+1);

  // OTOH, if an offset into the raw event is specified, get that
  if (dollar && !get_trace_format(dollar+1, &trace_off, &trace_sz)) {
    is_raw = true;
  } else {
    trace_off = 0;
    trace_sz = 0;
  }

  // If the user specified a period, make sure it is valid
  if (amp) {
    char *str_check = (char *)str;
    uint64_t buf = strtoll(amp+1, &str_check, 10);
    if (!*str_check)
      period = buf;
  }

  char path[2048] = {0};  // somewhat arbitrarily
  char buf[64] = {0};
  char *buf_copy = buf;
  int pathsz = snprintf(path, sizeof(path), "/sys/kernel/tracing/events/%s/%s/id", groupname, tracename);
  if (pathsz >= sizeof(path)) {
    // Possibly ran out of room
    return PTRET_BADFORMAT;
  }
  int fd = open(path, O_RDONLY);
  if (-1 == fd)
    return errno == ENOENT ? PTRET_NOEXIST : PTRET_BADPERMS;

  // Read the data in an eintr-safe way
  int read_ret = -1;
  long trace_id = 0;
  do {
    read_ret = read(fd, buf, sizeof(buf));
  } while (read_ret == -1 && errno == EINTR);
  close(fd);
  if ( read_ret > 0 )
    trace_id = strtol(buf, &buf_copy, 10);
  if (*buf_copy && *buf_copy != '\n')
    return PTRET_BADFORMAT;

  // Check enablement, just to print a log.  We still enable instrumentation.
  snprintf(path, sizeof(path), "/sys/kernel/tracing/events/%s/%s/enable", groupname, tracename);
  fd = open(path, O_RDONLY);
  if (-1 == fd || 1 != read(fd, buf, 1) || '0' != *buf) {
    LG_NTC("Tracepint %s:%s is not enabled.  Instrumentation will proceed, but you may not have any events.", groupname, tracename);
  } else {
    LG_NFO("Tracepoint %s:%s successfully enabled", groupname, tracename);
  }

  // OK done
  conf->id = trace_id;
  conf->period = period;
  conf->is_raw = is_raw;
  conf->reg = reg;
  conf->trace_off = trace_off;
  conf->trace_sz = trace_sz;
  return PTRET_OK;
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
