// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/prctl.h>

namespace ddprof {

class DumpableRestorer {
public:
  DumpableRestorer() : _was_dumpable{prctl(PR_GET_DUMPABLE) > 0} {}
  ~DumpableRestorer() { prctl(PR_SET_DUMPABLE, _was_dumpable ? 1 : 0); }

  DumpableRestorer(const DumpableRestorer &) = delete;
  DumpableRestorer operator=(const DumpableRestorer &) = delete;

private:
  bool _was_dumpable;
};

class DumpableGuard {
public:
  DumpableGuard() : _was_dumpable(prctl(PR_GET_DUMPABLE, 0) > 0) {
    if (!_was_dumpable) {
      prctl(PR_SET_DUMPABLE, 1);
    }
  }

  ~DumpableGuard() {
    if (!_was_dumpable) {
      prctl(PR_SET_DUMPABLE, 0);
    }
  }

  DumpableGuard(const DumpableGuard &) = delete;
  DumpableGuard operator=(const DumpableGuard &) = delete;

private:
  bool _was_dumpable;
};

} // namespace ddprof