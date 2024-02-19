// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#define _GNU_SOURCE // required for RTLD_NEXT

#include <assert.h>
#include <dlfcn.h>
#include <stddef.h>
#include <sys/stat.h>

#ifdef DEBUG
#  include <stdio.h>
#  include <stdlib.h>
#endif

#ifndef _STAT_VER_LINUX
#  ifndef __x86_64__
#    define _STAT_VER_LINUX 0
#  else
#    define _STAT_VER_LINUX 1
#  endif
#endif

void *dlsym(void *handle, const char *symbol) __attribute__((weak));

// fstat is linked statically on glibc < 2.35 and symbol is not present in
// libc.so.6 Provide a replacement that calls __fxstat if present or fstat
// resolved with dlsym/RTLD_NEXT
int __fxstat(int ver, int fd, struct stat *buf) __attribute__((weak));
int __xstat(int ver, const char *pathname, struct stat *buf)
    __attribute__((weak));

extern int fstat(int fd, struct stat *buf)
    __attribute__((weak, alias("__fstat")));

extern int stat(const char *pathname, struct stat *buf)
    __attribute__((weak, alias("__stat")));

extern int lstat(const char *pathname, struct stat *buf)
    __attribute__((weak, alias("__lstat")));

// NOLINTNEXTLINE(cert-dcl51-cpp)
__attribute__((unused)) int __fstat(int fd, struct stat *buf) {
  if (__fxstat) {
    // __fxstat is available call it directly
    return __fxstat(_STAT_VER_LINUX, fd, buf);
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
  assert(0);
  return -1;
}

// NOLINTNEXTLINE(cert-dcl51-cpp)
__attribute__((unused)) int __stat(const char *pathname, struct stat *buf) {
  if (__xstat) {
    // __xstat is available call it directly
    return __xstat(_STAT_VER_LINUX, pathname, buf);
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
  assert(0);
  return -1;
}

// NOLINTNEXTLINE(cert-dcl51-cpp)
__attribute__((unused)) int __lstat(const char *pathname, struct stat *buf) {
  static __typeof(lstat) *s_lstat = NULL;
  if (s_lstat == NULL && dlsym) {
    s_lstat = (__typeof(s_lstat))dlsym(RTLD_NEXT, "lstat");
  }
  if (s_lstat) {
    return s_lstat(pathname, buf);
  }

  // Should not happen
  assert(0);
  return -1;
}

extern void *__dso_handle __attribute__((__visibility__("hidden")));

int __register_atfork(void (*prepare)(void), void (*parent)(void),
                      void (*child)(void), void *dso_handle)
    __attribute__((weak));

extern int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                          void (*child)(void))
    __attribute__((weak, alias("__pthread_atfork")));

/* pthread_atfork is defined in libc_nonshared.a on aarch64 glibc, hence we need
 * to provide our own definition... */
int __pthread_atfork(void (*prepare)(void), void (*parent)(void),
                     void (*child)(void)) {
  static __typeof(pthread_atfork) *s_func = NULL;

  // if __register_atfork is available (glibc), call it directly
  if (__register_atfork) {
#ifdef DEBUG
    fprintf(stderr, "We call __register_atfork \n");
#endif
    return __register_atfork(prepare, parent, child, __dso_handle);
  }

  // we must be on musl, look up pthread_atfork
  if (s_func == NULL && dlsym) {
#ifdef DEBUG
    fprintf(stderr, "We look for pthread_atfork (musl code path)\n");
#endif
    s_func = (__typeof(s_func))dlsym(RTLD_NEXT, "pthread_atfork");
    if (s_func == NULL) {
      // We need to look for default symbol when preloading
      s_func = (__typeof(s_func))dlsym(RTLD_DEFAULT, "pthread_atfork");
      if (s_func == &__pthread_atfork) {
        // prevent infinite loop
        s_func = NULL;
      }
    }
  }

  if (s_func) {
#ifdef DEBUG
    fprintf(stderr, "return through s_func \n");
#endif
    return s_func(prepare, parent, child);
  }

  // Should not happen
#ifdef DEBUG
  fprintf(stderr, "FAIL \n");
#endif
  assert(0);
  return -1;
}
