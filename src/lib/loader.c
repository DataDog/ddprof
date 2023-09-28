// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "dd_profiling.h"
#include "lib_embedded_data.h"
#include "sha1.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Role of loader is to ensure that all dependencies (libdl/lim/libpthread) of
 * libdd_profiling-embedded.so are satisfied before dlopen'ing it.
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
 * libc.musl-x86_64.so.1), that is why libdd_loader and libdd_profiling do not
 * depend explicitly on libc but rely on the target process loading libc (this
 * will not work if target process does not depend on libc, but in that case,
 * libdd_profiling will not be able to intercept allocations anyway).
 * Loader load missing dependencies by dlopen'ing them, since dlopen may not be
 * available, it falls back to using __libc_dlopen_mode, an internal libc.so.6
 * function implementing dlopen, in that case.
 */
char *dlerror(void) __attribute__((weak));
void *dlopen(const char *filename, int flags) __attribute__((weak));
void *dlsym(void *handle, const char *symbol) __attribute__((weak));
// NOLINTNEXTLINE(cert-dcl51-cpp)
void *__libc_dlopen_mode(const char *filename, int flag) __attribute__((weak));
// NOLINTNEXTLINE(cert-dcl51-cpp)
void *__libc_dlsym(void *handle, const char *symbol) __attribute__((weak));
int pthread_cancel(pthread_t thread) __attribute__((weak));
double log(double x) __attribute__((weak));
int timer_create(clockid_t clockid, struct sigevent *sevp, timer_t *timerid)
    __attribute__((weak));

static void *s_libdl_handle = NULL;
static __typeof(dlerror) *s_dlerror = &dlerror;
static __typeof(dlopen) *s_dlopen = &dlopen;

static void ensure_libdl_is_loaded();

static void *my_dlopen(const char *filename, int flags) {
  if (!s_dlopen) {
    // if libdl.so is not loaded, use __libc_dlopen_mode
    s_dlopen = __libc_dlopen_mode;
  }
  if (s_dlopen) {
    void *ret = s_dlopen(filename, flags);
    if (!ret && s_dlerror) {
      fprintf(stderr, "Failed to dlopen %s (%s)\n", filename, s_dlerror());
    }
    return ret;
  }
  // Should not happen
  return NULL;
}

static void *my_dlsym(void *handle, const char *symbol) {
  static __typeof(dlsym) *dlsym_ptr = &dlsym;
  if (!dlsym_ptr) {
    // dlsym is not available: meaning we are on glibc and libdl.so was not
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

static void ensure_libdl_is_loaded() {
  if (!dlsym && !s_libdl_handle) {
    s_libdl_handle = my_dlopen("libdl.so.2", RTLD_GLOBAL | RTLD_NOW);
  }

  if (s_libdl_handle) {
    // now that we have loaded libdl, we can ensure that we use the real
    // dlopen function (instead of internal libc function)
    if (s_dlopen == __libc_dlopen_mode) {
      s_dlopen = (__typeof(dlopen) *)my_dlsym(s_libdl_handle, "dlopen");
    }
    if (!s_dlerror) {
      s_dlerror = (__typeof(dlerror) *)my_dlsym(s_libdl_handle, "dlerror");
    }
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

static void ensure_librt_is_loaded() {
  if (!timer_create) {
    my_dlopen("librt.so.1", RTLD_GLOBAL | RTLD_NOW);
  }
}

static const char *temp_directory_path() {
  const char *tmpdir = NULL;
  const char *env[] = {"TMPDIR", "TMP", "TEMP", "TEMPDIR", NULL};
  for (const char **e = env; !tmpdir && *e; ++e) {
    tmpdir = getenv(*e);
  }
  const char *p = tmpdir ? tmpdir : "/tmp";
  struct stat st;
  if (!(stat(p, &st) == 0 && S_ISDIR(st.st_mode))) {
    p = NULL;
  }

  return p;
}

static char *create_temp_file(const char *prefix, EmbeddedData data,
                              mode_t mode) {
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

static char *get_or_create_temp_file(const char *prefix, EmbeddedData data,
                                     mode_t mode) {
  const char *tmp_dir = temp_directory_path();
  if (!tmp_dir) {
    return NULL;
  }

  unsigned char digest[20];
  char str_digest[41];
  SHA1(digest, data.data, data.size);
  SHA1StrDigest(digest, str_digest);

  char *path =
      malloc(strlen(tmp_dir) + strlen(prefix) + sizeof(str_digest) + 2);
  if (path == NULL) {
    return NULL;
  }
  strcpy(path, tmp_dir);
  strcat(path, "/");
  strcat(path, prefix);
  strcat(path, "-");
  strcat(path, str_digest);

  // Check if file already exists
  struct stat st;
  if (stat(path, &st) == 0) {
    return path;
  }

  char *tmp_path = create_temp_file(prefix, data, mode);
  if (!tmp_path) {
    free(path);
    return NULL;
  }

  if (rename(tmp_path, path) != 0) {
    unlink(tmp_path);
    free(tmp_path);
    free(path);
    return NULL;
  }

  free(tmp_path);
  return path;
}

static void *s_profiling_lib_handle = NULL;
__typeof(ddprof_start_profiling) *s_start_profiling_func = NULL;
__typeof(ddprof_stop_profiling) *s_stop_profiling_func = NULL;

static void __attribute__((constructor)) loader() {
  char *lib_profiling_path = getenv(k_profiler_lib_env_variable);
  if (!lib_profiling_path) {
    EmbeddedData lib_data = profiling_lib_data();
    EmbeddedData exe_data = profiler_exe_data();
    if (lib_data.size == 0 || exe_data.size == 0) {
      // nothing to do
      return;
    }
    lib_profiling_path = get_or_create_temp_file(
        k_libdd_profiling_embedded_name, lib_data, 0644);
    char *profiler_exe_path =
        get_or_create_temp_file(k_profiler_exe_name, exe_data, 0755);
    if (!lib_profiling_path || !profiler_exe_path) {
      free(lib_profiling_path);
      free(profiler_exe_path);
      return;
    }
    setenv(k_profiler_ddprof_exe_env_variable, profiler_exe_path, 1);
    free(profiler_exe_path);
  } else {
    lib_profiling_path = strdup(lib_profiling_path);
    if (!lib_profiling_path) {
      return;
    }
  }

  ensure_libdl_is_loaded();
  ensure_libm_is_loaded();
  ensure_libpthread_is_loaded();
  ensure_librt_is_loaded();

  s_profiling_lib_handle = my_dlopen(lib_profiling_path, RTLD_LOCAL | RTLD_NOW);
  free(lib_profiling_path);
  if (s_profiling_lib_handle) {
    s_start_profiling_func = (__typeof(s_start_profiling_func))my_dlsym(
        s_profiling_lib_handle, "ddprof_start_profiling");
    s_stop_profiling_func = (__typeof(s_stop_profiling_func))my_dlsym(
        s_profiling_lib_handle, "ddprof_stop_profiling");
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
