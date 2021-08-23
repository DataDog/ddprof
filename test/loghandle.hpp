#pragma once

extern "C" {
#include "logger.h"
}

class LogHandle {
public:
  LogHandle() {
    LOG_open(LOG_STDERR, nullptr);
    LOG_setlevel(LL_DEBUG);
  }
  ~LogHandle() { LOG_close(); }
};
