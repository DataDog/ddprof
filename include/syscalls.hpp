// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace ddprof {
inline pid_t gettid() { return syscall(SYS_gettid); }

inline int memfd_create(const char *name, unsigned int flags) {
  return syscall(SYS_memfd_create, name, flags);
}

inline int futex(uint32_t *uaddr, int futex_op, uint32_t val,
                 const struct timespec *timeout, uint32_t *uaddr2,
                 uint32_t val3) {
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

inline int rt_tgsigqueueinfo(int tgid, int tid, int sig, siginfo_t *uinfo) {
  return syscall(SYS_rt_tgsigqueueinfo, tgid, tid, sig, uinfo);
}

} // namespace ddprof
