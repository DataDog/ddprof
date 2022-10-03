// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "dd_profiling.h"
#include "ddres_helpers.hpp"
#include "defer.hpp"
#include "lib_embedded_data.hpp"
#include "loghandle.hpp"
#include "tempfile.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Role of loader is to ensure that all dependencies (libdl/lim/lipthread) of
 * libdd_profiling-embedded.so are satisfied before dlopening it.
 * On musl, all libc features are in libc.so and hence are available once libc
 * is loaded.
 * Whereas on glibc, some features are in their own lib (this is
 * changing with recent glibc versions, where most features are moved back to
 * libc.so.6):
 * - libdl: dlopen/dlsym
 * - libm: math functions (log,...)
 * - libpthread: threading functions (pthread_create,...)
 * Therefore when executing on glibc, some required dependencies
 * (dlopen/dlsym/pthread/log) of libdd_profiling might not be loaded, that's why
 * libdd_profiling is indirectly loaded by loader after ensuring that these
 * dependencies are available.
 * Note that libdd_profiling cannot depend on libdl/libm/libpthread since those
 * do not exist on musl. Even libc has a different name (libc.so.6 vs
 * libc.musl-x86_64.so.1), that is why libloader and libdd_profiling do not
 * depend explicitly on libc but rely on the target process loading libc (this
 * will not work if target process does not depend on libc, but in that case,
 * libdd_profiling will not be able to intercept allocations anyway).
 * Loader load missing dependencies by dlopening them., since dlopen may not be
 * available, it falls back to using __libc_dlopen_mode, an internal libc.so.6
 * function implementing dlopen, in that case.
 */
extern "C" {
void *dlopen(const char *filename, int flags) noexcept __attribute__((weak));
void *dlsym(void *handle, const char *symbol) noexcept __attribute__((weak));
// NOLINTNEXTLINE cert-dcl51-cpp
void *__libc_dlopen_mode(const char *filename, int flag) noexcept
    __attribute__((weak));
// NOLINTNEXTLINE cert-dcl51-cpp
void *__libc_dlsym(void *handle, const char *symbol) __attribute__((weak));
int pthread_cancel(pthread_t thread) __attribute__((weak));
double log(double x) __attribute__((weak));
}

static void *s_libdl_handle = NULL;

static void *my_dlopen(const char *filename, int flags) {
  static decltype(dlopen) *dlopen_ptr = &dlopen;

  if (!dlopen_ptr) {
    // if libdl.so is not loaded, use __libc_dlopen_mode
    dlopen_ptr = __libc_dlopen_mode;
  }
  if (dlopen_ptr) {
    return dlopen_ptr(filename, flags);
  }

  // Should not happen
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
  static decltype(dlsym) *dlsym_ptr = &dlsym;
  if (!dlsym_ptr) {
    // dlysm is not available: meaning we are on glibc and libdl.so was not
    // loaded at startup

    // First ensure that libdl.so is loaded
    if (!s_libdl_handle) {
      ensure_libdl_is_loaded();
    }

    if (s_libdl_handle) {
      // locate dlsym in libdl.so by using internal libc.so.6 function
      // __libc_dlsym.
      // Note that we need dlsym because __libc_dlsym does not provide
      // RTLD_DEFAULT/RTLD_NEXT functionality.
      dlsym_ptr = reinterpret_cast<decltype(dlsym) *>(
          __libc_dlsym(s_libdl_handle, "dlsym"));
    }

    // Should not happen
    if (!dlsym_ptr) {
      return NULL;
    }
  }

  return dlsym_ptr(handle, symbol);
}

typedef int (*FstatFunc)(int, struct stat *) noexcept;

// fstat is linked statically on glibc and symbol is not present in libc.so.6
// Provide a replacement that calls __fxstat is present or fstat resolved with
// dlsym/RTLD_NEXT
extern "C" int __fxstat(int ver, int fd, struct stat *buf)
    __attribute__((weak));

extern int fstat(int fd, struct stat *buf)
    __attribute__((weak, alias("__fstat")));

// NOLINTNEXTLINE cert-dcl51-cpp
extern "C" __attribute__((unused)) int __fstat(int fd, struct stat *buf) {
  if (__fxstat) {
    // __fxstat is available call it directly
    return __fxstat(1, fd, buf);
  }
  // __fxtstat is not available, we must be executing on musl, therefore `fstat`
  // should be availble in libc
  static declatype(fstat) *s_fstat = NULL;
  if (s_fstat == NULL) {
    s_fstat = reinterpret_cast<FstatFunc>(my_dlsym(RTLD_NEXT, "fstat"));
  }
  if (s_fstat) {
    return s_fstat(fd, buf);
  }

  // Should not happen
  return -1;
}

static std::string s_lib_profiling_path;
static std::string s_profiler_exe_path;
static void *s_profiling_lib_handle = nullptr;
decltype(ddprof_start_profiling) *s_start_profiling_func = nullptr;
decltype(ddprof_stop_profiling) *s_stop_profiling_func = nullptr;

static DDRes __attribute__((constructor)) loader() {
  LogHandle log_handle(LL_WARNING);

  const char *s = getenv(k_profiler_lib_env_variable);
  if (!s) {
    auto lib_data = ddprof::profiling_lib_data();
    auto exe_data = ddprof::profiler_exe_data();
    if (lib_data.empty() || exe_data.empty()) {
      // nothing to do
      return {};
    }
    DDRES_CHECK_FWD(create_temp_file(k_libdd_profiling_name, lib_data, 0644,
                                     s_lib_profiling_path));
    create_temp_file(k_profiler_exe_name, exe_data, 0755, s_profiler_exe_path);
    s = s_lib_profiling_path.c_str();
    setenv(k_profiler_ddprof_exe_env_variable, s_profiler_exe_path.c_str(), 1);
  }

  ensure_libdl_is_loaded();
  ensure_libm_is_loaded();
  ensure_libpthread_is_loaded();

  s_profiling_lib_handle = my_dlopen(s, RTLD_LOCAL | RTLD_NOW);
  if (s_profiling_lib_handle) {
    s_start_profiling_func = reinterpret_cast<decltype(s_start_profiling_func)>(
        my_dlsym(s_profiling_lib_handle, "ddprof_start_profiling"));
    s_stop_profiling_func = reinterpret_cast<decltype(s_stop_profiling_func)>(
        my_dlsym(s_profiling_lib_handle, "ddprof_stop_profiling"));
  }
  return {};
}

static void __attribute__((destructor)) unloader() {
  if (!s_lib_profiling_path.empty()) {
    unlink(s_lib_profiling_path.c_str());
  }
  if (!s_profiler_exe_path.empty()) {
    unlink(s_profiler_exe_path.c_str());
  }
}

// We need to provide libdd_profiling-embedded.so interface and forward to it
// because user needs to be able to link with libdd_profiling.so (which is just
// the loader).
int ddprof_start_profiling() {
  return s_start_profiling_func ? s_start_profiling_func() : -1;
}

void ddprof_stop_profiling(int timeout_ms) {
  if (s_stop_profiling_func) {
    s_stop_profiling_func(timeout_ms);
  }
}
