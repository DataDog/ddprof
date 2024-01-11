// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {

class ReentryGuard {
public:
  explicit ReentryGuard(bool *reentry_guard) : _reentry_guard(reentry_guard) {
    if (_reentry_guard) {
      _ok = (!*_reentry_guard);
      *_reentry_guard = true;
    }
  }

  ~ReentryGuard() {
    if (_ok) {
      *_reentry_guard = false;
    }
  }

  explicit operator bool() const { return _ok; }

  ReentryGuard(const ReentryGuard &) = delete;
  ReentryGuard &operator=(const ReentryGuard &) = delete;

private:
  bool *_reentry_guard;
  bool _ok{false};
};

} // namespace ddprof
