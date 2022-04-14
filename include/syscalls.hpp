// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/syscall.h>
#include <unistd.h>

namespace ddprof {
static inline pid_t gettid() { return syscall(SYS_gettid); }

static inline int memfd_create(const char *name, unsigned int flags) {
  return syscall(SYS_memfd_create, name, flags);
}
} // namespace ddprof
