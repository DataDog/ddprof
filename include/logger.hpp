// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "unlikely.hpp"
#include "version.hpp"

#include <chrono>
#include <functional>
#include <stdarg.h>

namespace ddprof {

enum LOG_OPTS {
  LOG_DISABLE = 0,
  LOG_SYSLOG = 1,
  LOG_STDOUT = 2,
  LOG_STDERR = 3,
  LOG_FILE = 4,
};

enum LOG_LVL {
  LL_FORCE_ALERT = -1,
  LL_FORCE_CRITICAL = -2,
  LL_FORCE_ERROR = -3,
  LL_FORCE_WARNING = -4,
  LL_FORCE_NOTICE = -5,
  LL_FORCE_INFORMATIONAL = -6,
  LL_FORCE_DEBUG = -7,
  LL_EMERGENCY = 0, // No force override because always printed
  LL_ALERT = 1,
  LL_CRITICAL = 2,
  LL_ERROR = 3,
  LL_WARNING = 4,
  LL_NOTICE = 5,
  LL_INFORMATIONAL = 6,
  LL_DEBUG = 7,
  LL_LENGTH,
};

enum LOG_FACILITY {
  LF_KERNEL = 0,
  LF_USER = 1,
  LF_MAIL = 2,
  LF_SYSTEM = 3,
  LF_SECURITY = 4,
  LF_SYSLOGD = 5,
  LF_LINE = 6,
  LF_NETNEWS = 7,
  LF_UUCP = 8,
  LF_CLOCK = 9,
  LF_SEC2 = 10,
  LF_FTP = 11,
  LF_NTP = 12,
  LF_AUDIT = 13,
  LF_ALERT = 14,
  LF_CLOCK2 = 15,
  LF_LOCAL0 = 16,
  LF_LOCAL1 = 17,
  LF_LOCAL2 = 18,
  LF_LOCAL3 = 19,
  LF_LOCAL4 = 20,
  LF_LOCAL5 = 21,
  LF_LOCAL6 = 22,
  LF_LOCAL7 = 23,
};

// Allow for compile-time argument type checking for printf-like functions
#define printflike(x, y) __attribute__((format(printf, x, y)))

// Manage the logging backend
bool LOG_syslog_open();
void LOG_close();
bool LOG_open(int mode, const char *opts);

// Formatted print to the ddprof logging facility
// Log-print-Formatted with Level, Facility, and Name
printflike(4, 5) void lprintfln(int lvl, int fac, const char *name,
                                const char *fmt, ...);

// Same as above, but suppress printing if the level isn't high enough
// O for optional
printflike(4, 5) void olprintfln(int lvl, int fac, const char *name,
                                 const char *fmt, ...);

// Same as the first, but with a single variadic arg instead of ...
// V for variadic, as per libc's v*printf() functions
void vlprintfln(int lvl, int fac, const char *name, const char *format,
                va_list args);

// Setters for global logger context
void LOG_setname(const char *name);
void LOG_setlevel(int lvl);
int LOG_getlevel();
void LOG_setfacility(int fac);

void LOG_setratelimit(uint64_t max_log_per_interval,
                      std::chrono::nanoseconds interval);

bool LOG_is_logging_enabled_for_level(int level);

using LogsAllowedCallback = std::function<bool()>;

// Allow to inject a function used by logger to check if logs are allowed
void LOG_set_logs_allowed_function(LogsAllowedCallback logs_allowed_function);

/******************************* Logging Macros *******************************/
#define ABS(__x)                                                               \
  ({                                                                           \
    const typeof(__x) _x = (__x);                                              \
    _x < 0 ? -1 * _x : _x;                                                     \
  })

// Avoid calling arguments (which can have CPU costs unless level is OK)
#define LG_IF_LVL_OK(level, ...)                                               \
  do {                                                                         \
    if (unlikely(LOG_is_logging_enabled_for_level(level))) {                   \
      ddprof::olprintfln(ABS(level), -1, MYNAME, __VA_ARGS__);                         \
    }                                                                          \
  } while (false)

#define LG_ERR(...) LG_IF_LVL_OK(ddprof::LL_ERROR, __VA_ARGS__)
#define LG_WRN(...) LG_IF_LVL_OK(ddprof::LL_WARNING, __VA_ARGS__)
#define LG_NTC(...) LG_IF_LVL_OK(ddprof::LL_NOTICE, __VA_ARGS__)
#define LG_NFO(...) LG_IF_LVL_OK(ddprof::LL_INFORMATIONAL, __VA_ARGS__)
#define LG_DBG(...) LG_IF_LVL_OK(ddprof::LL_DEBUG, __VA_ARGS__)
#define PRINT_NFO(...) LG_IF_LVL_OK(-1 * LL_INFORMATIONAL, __VA_ARGS__)

} // namespace ddprof
