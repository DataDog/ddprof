// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "dd_profiling.h"
#include "lib_embedded_data.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
void *dlopen(const char *filename, int flags) __attribute__((weak));
void *dlsym(void *handle, const char *symbol) __attribute__((weak));
// NOLINTNEXTLINE cert-dcl51-cpp
void *__libc_dlopen_mode(const char *filename, int flag) __attribute__((weak));
// NOLINTNEXTLINE cert-dcl51-cpp
void *__libc_dlsym(void *handle, const char *symbol) __attribute__((weak));
int pthread_cancel(pthread_t thread) __attribute__((weak));
double log(double x) __attribute__((weak));

static void *s_libdl_handle = NULL;

static void *my_dlopen(const char *filename, int flags) {
  static __typeof(dlopen) *dlopen_ptr = &dlopen;

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
  static __typeof(dlsym) *dlsym_ptr = &dlsym;
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
      dlsym_ptr = (__typeof(dlsym_ptr))__libc_dlsym(s_libdl_handle, "dlsym");
    }

    // Should not happen
    if (!dlsym_ptr) {
      return NULL;
    }
  }

  return dlsym_ptr(handle, symbol);
}

const char *temp_directory_path() {
  const char *tmpdir = NULL;
  const char *env[] = {"TMPDIR", "TMP", "TEMP", "TEMPDIR", NULL};
  for (const char **e = env; tmpdir && *e != NULL; ++e) {
    tmpdir = getenv(*e);
  }
  const char *p = tmpdir ? tmpdir : "/tmp";
  struct stat st;
  if (!(stat(p, &st) == 0 && S_ISDIR(st.st_mode))) {
    p = NULL;
  }

  return p;
}

char *create_temp_file(const char *prefix, EmbeddedData data, mode_t mode) {
  const char *tmp_dir = temp_directory_path();
  if (!tmp_dir) {
    return NULL;
  }

  char *path = malloc(strlen(tmp_dir) + strlen(prefix) + strlen(".XXXXXX") + 2);
  if (path == NULL) {
    return NULL;
  }
  strcpy(path, tmp_dir);
  strcat(path, "/");
  strcat(path, prefix);
  strcat(path, ".XXXXXX");

  // Create temporary file
  int fd = mkostemp(path, O_CLOEXEC);
  if (fd == -1) {
    free(path);
    return NULL;
  }

  if (fchmod(fd, mode) != 0) {
    close(fd);
    unlink(path);
    free(path);
    return NULL;
  }

  // Write embedded lib into temp file
  if (write(fd, data.data, data.size) != (ssize_t)data.size) {
    close(fd);
    unlink(path);
    free(path);
    return NULL;
  }

  close(fd);
  return path;
}

static char *s_lib_profiling_path = NULL;
static char *s_profiler_exe_path = NULL;
static void *s_profiling_lib_handle = NULL;
__typeof(ddprof_start_profiling) *s_start_profiling_func = NULL;
__typeof(ddprof_stop_profiling) *s_stop_profiling_func = NULL;

static void __attribute__((constructor)) loader() {
  const char *s = getenv(k_profiler_lib_env_variable);
  if (!s) {
    EmbeddedData lib_data = profiling_lib_data();
    EmbeddedData exe_data = profiler_exe_data();
    if (lib_data.size == 0 || exe_data.size == 0) {
      // nothing to do
      return;
    }
    s_lib_profiling_path =
        create_temp_file(k_libdd_profiling_name, lib_data, 0644);
    s_profiler_exe_path = create_temp_file(k_profiler_exe_name, exe_data, 0755);
    if (!s_lib_profiling_path || !s_profiler_exe_path) {
      return;
    }
    s = s_lib_profiling_path;
    setenv(k_profiler_ddprof_exe_env_variable, s_profiler_exe_path, 1);
  }

  ensure_libdl_is_loaded();
  ensure_libm_is_loaded();
  ensure_libpthread_is_loaded();

  s_profiling_lib_handle = my_dlopen(s, RTLD_LOCAL | RTLD_NOW);
  if (s_profiling_lib_handle) {
    s_start_profiling_func = (__typeof(s_start_profiling_func))my_dlsym(
        s_profiling_lib_handle, "ddprof_start_profiling");
    s_stop_profiling_func = (__typeof(s_stop_profiling_func))my_dlsym(
        s_profiling_lib_handle, "ddprof_stop_profiling");
  }

  return;
}

static void __attribute__((destructor)) unloader() {
  if (s_lib_profiling_path) {
    unlink(s_lib_profiling_path);
    free(s_lib_profiling_path);
  }
  if (s_profiler_exe_path) {
    unlink(s_profiler_exe_path);
    free(s_profiler_exe_path);
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
