// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "logger_setup.hpp"

#include "ddprof_cmdline.hpp"
#include "logger.hpp"

#include <array>

namespace ddprof {

void setup_logger(const char *log_mode, const char *log_level,
                  uint64_t max_log_per_sec_for_non_debug) {
  // Process logging mode
  char const *logpattern[] = {"stdout", "stderr", "syslog", "disabled"};
  int const idx_log_mode = log_mode
      ? arg_which(log_mode, logpattern, std::size(logpattern))
      : 0; // default to stdout
  switch (idx_log_mode) {
  case 0:
    LOG_open(LOG_STDOUT, "");
    break;
  case 1:
    LOG_open(LOG_STDERR, "");
    break;
  case 2:
    LOG_open(LOG_SYSLOG, "");
    break;
  case 3:
    LOG_open(LOG_DISABLE, "");
    break;
  default:
    LOG_open(LOG_FILE, log_mode);
    break;
  }

  // Process logging level
  char const *loglpattern[] = {"debug", "informational", "notice", "warn",
                               "error"};
  switch (arg_which(log_level, loglpattern, std::size(loglpattern))) {
  case 0:
    LOG_setlevel(LL_DEBUG);
    break;
  case 1:
    LOG_setlevel(LL_INFORMATIONAL);
    break;
  case 2:
    LOG_setlevel(LL_NOTICE);
    break;
  case 4:
    LOG_setlevel(LL_ERROR);
    break;
  case -1: // default
  case 3:
  default:
    LOG_setlevel(LL_WARNING);
    break;
  }

  if (LOG_getlevel() > LL_DEBUG) {
    LOG_setratelimit(max_log_per_sec_for_non_debug, std::chrono::seconds(1));
  }
}

} // namespace ddprof
