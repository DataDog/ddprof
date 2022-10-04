// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <dlfcn.h>
#include <stddef.h>
#include <sys/stat.h>

void *dlsym(void *handle, const char *symbol) __attribute__((weak));

// fstat is linked statically on glibc and symbol is not present in libc.so.6
// Provide a replacement that calls __fxstat is present or fstat resolved with
// dlsym/RTLD_NEXT
int __fxstat(int ver, int fd, struct stat *buf) __attribute__((weak));
int __xstat(int ver, const char *pathname, struct stat *buf)
    __attribute__((weak));

extern int fstat(int fd, struct stat *buf)
    __attribute__((weak, alias("__fstat")));

extern int stat(const char *pathname, struct stat *buf)
    __attribute__((weak, alias("__stat")));

// NOLINTNEXTLINE cert-dcl51-cpp
__attribute__((unused)) int __fstat(int fd, struct stat *buf) {
  if (__fxstat) {
    // __fxstat is available call it directly
    return __fxstat(1, fd, buf);
  }
  // __fxtstat is not available, we must be executing on musl, therefore `fstat`
  // should be availble in libc
  static __typeof(fstat) *s_fstat = NULL;
  if (s_fstat == NULL && dlsym) {
    s_fstat = (__typeof(s_fstat))dlsym(RTLD_NEXT, "fstat");
  }
  if (s_fstat) {
    return s_fstat(fd, buf);
  }

  // Should not happen
  return -1;
}

// NOLINTNEXTLINE cert-dcl51-cpp
__attribute__((unused)) int __stat(const char *pathname, struct stat *buf) {
  if (__xstat) {
    // __xstat is available call it directly
    return __xstat(1, pathname, buf);
  }
  // __xtstat is not available, we must be executing on musl, therefore `stat`
  // should be availble in libc
  static __typeof(stat) *s_stat = NULL;
  if (s_stat == NULL && dlsym) {
    s_stat = (__typeof(s_stat))dlsym(RTLD_NEXT, "stat");
  }
  if (s_stat) {
    return s_stat(pathname, buf);
  }

  // Should not happen
  return -1;
}
