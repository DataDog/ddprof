// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "logger.h"
}

class LogHandle {
public:
  LogHandle(int lvl = LL_DEBUG) {
    LOG_open(LOG_STDERR, nullptr);
    LOG_setlevel(lvl);
  }
  ~LogHandle() { LOG_close(); }
};
