// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void *dlopen(const char *filename, int flags) __attribute__((weak));
void *dlsym(void *handle, const char *symbol) __attribute__((weak));
void *__libc_dlopen_mode(const char *filename, int flag) __attribute__((weak));
void *__libc_dlsym(void *handle, const char *symbol) __attribute__((weak));
int pthread_cancel(pthread_t thread) __attribute__((weak));
double log(double x) __attribute__((weak));

static void *s_libdl_handle = NULL;

static void *my_dlopen(const char *filename, int flags) {
  static __typeof(dlopen) *dlopen_ptr = &dlopen;

  if (!dlopen_ptr) {
    dlopen_ptr = __libc_dlopen_mode;
  }
  if (dlopen_ptr) {
    return dlopen_ptr(filename, flags);
  }
  return NULL;
}

static void ensure_libdl_is_loaded() {
  if (!dlsym && !s_libdl_handle) {
    s_libdl_handle = my_dlopen("libdl.so.2", RTLD_GLOBAL | RTLD_NOW);
  }
}

static void ensure_libm_is_loaded() {
  if (!log) {
    my_dlopen("libm.so.6", RTLD_GLOBAL | RTLD_NOW);
  }
}

static void ensure_libpthread_is_loaded() {
  if (!pthread_cancel) {
    my_dlopen("libpthread.so.0", RTLD_GLOBAL | RTLD_NOW);
  }
}

static void *my_dlsym(void *handle, const char *symbol) {
  static __typeof(dlsym) *dlsym_ptr = &dlsym;
  if (!dlsym_ptr) {
    if (!s_libdl_handle) {
      ensure_libdl_is_loaded();
    }

    if (s_libdl_handle) {
      dlsym_ptr = (__typeof(dlsym) *)__libc_dlsym(s_libdl_handle, "dlsym");
    }
    if (!dlsym_ptr) {
      return NULL;
    }
  }

  return dlsym_ptr(handle, symbol);
}

// fstat is linked statically on glibc and  symbol is not present in libc.so.6
// Provide a replacement that calls __fxstat is present or fstat resolved with
// dlsym/RTLD_NEXT
int __fxstat(int ver, int fd, struct stat *buf) __attribute__((weak));

extern int fstat(int fd, struct stat *buf)
    __attribute__((weak, alias("__fstat")));

__attribute__((unused)) static int __fstat(int fd, struct stat *buf) {
  if (__fxstat) {
    return __fxstat(1, fd, buf);
  }
  static __typeof(fstat) *s_fstat = NULL;
  if (s_fstat == NULL) {
    s_fstat = my_dlsym(RTLD_NEXT, "fstat");
  }
  if (s_fstat) {
    return s_fstat(fd, buf);
  }
  return -1;
}

static void __attribute__((constructor)) loader() {
  const char *s = getenv(k_profiler_lib_env_variable);
  if (!s) {
    // nothing to do
    return;
  }

  ensure_libdl_is_loaded();
  ensure_libm_is_loaded();
  ensure_libpthread_is_loaded();

  my_dlopen(s, RTLD_LOCAL | RTLD_NOW);
}
