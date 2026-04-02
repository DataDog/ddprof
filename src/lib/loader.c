// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "dd_profiling.h"
#include "lib_embedded_data.h"
#include "libdd_profiling-embedded_hash.h"
#include "sha256.h"
#include "tls_state_storage.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

__attribute__((__visibility__("default")))
__attribute__((tls_model("initial-exec")))
__attribute__((aligned(DDPROF_TLS_STATE_ALIGN))) __thread char
    ddprof_lib_state[DDPROF_TLS_STATE_SIZE];

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
int dladdr(const void *addr, Dl_info *info) __attribute__((weak));
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
static __typeof(dladdr) *s_dladdr = &dladdr;

static void ensure_libdl_is_loaded();

static void *my_dlopen_impl(const char *filename, int flags, int silent) {
  if (!s_dlopen) {
    // if libdl.so is not loaded, use __libc_dlopen_mode
    s_dlopen = __libc_dlopen_mode;
  }
  if (s_dlopen) {
    void *ret = s_dlopen(filename, flags);
    if (!ret && !silent && s_dlerror) {
      fprintf(stderr, "Failed to dlopen %s (%s)\n", filename, s_dlerror());
    }
    return ret;
  }
  // Should not happen
  return NULL;
}

static void *my_dlopen(const char *filename, int flags) {
  return my_dlopen_impl(filename, flags, 0);
}

static void *my_dlopen_silent(const char *filename, int flags) {
  return my_dlopen_impl(filename, flags, 1);
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
    if (!s_dladdr) {
      s_dladdr = (__typeof(dladdr) *)my_dlsym(s_libdl_handle, "dladdr");
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

// When the loader is dlopen'd with RTLD_GLOBAL, glibc does not promote its
// symbols to global scope until dlopen returns. The embedded .so references
// ddprof_lib_state (defined here in the loader) as an undefined symbol, so
// loading it with RTLD_NOW during our constructor fails with
// "undefined symbol: ddprof_lib_state".
//
// Fix: re-open ourselves with RTLD_NOLOAD | RTLD_GLOBAL to promote our
// symbols before loading the embedded .so. When loaded via LD_PRELOAD,
// symbols are already in global scope so this is a harmless no-op.
//
// On musl, RTLD_NOLOAD with a bare SONAME fails because musl tracks loaded
// libraries by their actual path, not their SONAME. Use dladdr() to find
// the loader's actual path first, which works on both musl and glibc.
static void ensure_loader_symbols_promoted() {
#ifdef DDPROF_LOADER_SONAME
  // Use dladdr() to find the actual path of this library at runtime.
  // This is necessary on musl, where RTLD_NOLOAD with a bare SONAME fails
  // because musl tracks loaded libraries by their full path, not their SONAME.
  Dl_info info;
  if (s_dladdr && s_dladdr((void *)ddprof_start_profiling, &info) &&
      info.dli_fname) {
    void *self =
        my_dlopen_silent(info.dli_fname, RTLD_GLOBAL | RTLD_NOLOAD | RTLD_NOW);
    if (self) {
      return;
    }
  }
  // Fallback: try by SONAME (works on glibc when dladdr is unavailable or
  // returns a different path than what the dynamic linker tracks).
  void *self = my_dlopen_silent(DDPROF_LOADER_SONAME,
                                RTLD_GLOBAL | RTLD_NOLOAD | RTLD_NOW);
  if (!self) {
    fprintf(stderr,
            "ddprof loader: failed to promote symbols to global scope "
            "-- embedded library may fail to load\n");
  }
#endif
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

  // +3 for '/' separator, '-' separator, and NUL terminator
  char *path = malloc(strlen(tmp_dir) + 1 + strlen(prefix) + 1 +
                      strlen(data.digest) + 1);
  if (path == NULL) {
    return NULL;
  }
  strcpy(path, tmp_dir);
  strcat(path, "/");
  strcat(path, prefix);
  strcat(path, "-");
  strcat(path, data.digest);

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

// ---------------------------------------------------------------------------
// Find the installed libdd_profiling-embedded.so next to the loader and verify
// its SHA-256 matches the build-time hash before dlopen'ing it.
// Returns a malloc'd path on success, NULL if not found or hash mismatch.
// Caller must free().
// ---------------------------------------------------------------------------
static char *find_installed_profiling_lib() {
  Dl_info info;
  if (!s_dladdr || !s_dladdr((void *)ddprof_start_profiling, &info) ||
      !info.dli_fname) {
    return NULL;
  }
  const char *last_slash = strrchr(info.dli_fname, '/');
  if (!last_slash) {
    return NULL;
  }
  size_t dir_len = last_slash - info.dli_fname;
  size_t name_len = strlen(k_libdd_profiling_embedded_name);
  char *lib_path = malloc(dir_len + 1 + name_len + 1);
  if (!lib_path) {
    return NULL;
  }
  memcpy(lib_path, info.dli_fname, dir_len);
  lib_path[dir_len] = '/';
  memcpy(lib_path + dir_len + 1, k_libdd_profiling_embedded_name, name_len + 1);

  int fd = open(lib_path, O_RDONLY | O_CLOEXEC);
  if (fd == -1) {
    free(lib_path);
    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size == 0) {
    close(fd);
    free(lib_path);
    return NULL;
  }

  void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    free(lib_path);
    return NULL;
  }

  unsigned char hash[32];
  sha256((const unsigned char *)mapped, st.st_size, hash);
  munmap(mapped, st.st_size);

  char hex[65];
  sha256_hex(hash, hex);

  if (strcmp(hex, libdd_profiling_embedded_hash) != 0) {
    fprintf(stderr,
            "ddprof: hash mismatch for installed %s "
            "(got %.16s..., expected %.16s...); "
            "falling back to embedded library.\n",
            k_libdd_profiling_embedded_name, hex,
            libdd_profiling_embedded_hash);
    free(lib_path);
    return NULL;
  }

  return lib_path;
}

// Find the ddprof executable relative to the loader's own on-disk location.
// The loader, the installed embedded lib, and the ddprof exe are all part of
// the same package, so they share a common install prefix.
// Checks:
//   1. <loader_dir>/../bin/ddprof  — standard install layout
//   2. <loader_dir>/ddprof         — flat build/dev layout
// Returns a malloc'd path on success, NULL if not found.  Caller must free().
static char *find_ddprof_exe() {
  Dl_info info;
  if (!s_dladdr || !s_dladdr((void *)ddprof_start_profiling, &info) ||
      !info.dli_fname) {
    return NULL;
  }
  const char *last_slash = strrchr(info.dli_fname, '/');
  if (!last_slash) {
    return NULL;
  }
  size_t dir_len = last_slash - info.dli_fname;
  static const char *const candidates[] = {
      "/../bin/ddprof", // standard install: <prefix>/ddprof/{lib,bin}/
      "/ddprof",        // flat build/dev layout
      NULL,
  };
  for (const char *const *rel = candidates; *rel; ++rel) {
    size_t rel_len = strlen(*rel);
    char *exe_path = malloc(dir_len + rel_len + 1);
    if (!exe_path) {
      continue;
    }
    memcpy(exe_path, info.dli_fname, dir_len);
    memcpy(exe_path + dir_len, *rel, rel_len + 1);
    if (access(exe_path, X_OK) == 0) {
      return exe_path;
    }
    free(exe_path);
  }
  return NULL;
}

// Extract both the profiling library and the ddprof binary from embedded data
// to /tmp and load the library.  Returns the dlopen handle or NULL on failure.
static void *load_embedded_profiling_lib() {
  EmbeddedData lib_data = profiling_lib_data();
  EmbeddedData exe_data = profiler_exe_data();
  if (lib_data.size == 0 || exe_data.size == 0) {
    return NULL;
  }
  char *lib_path =
      get_or_create_temp_file(k_libdd_profiling_embedded_name, lib_data, 0644);
  char *exe_path = get_or_create_temp_file(k_profiler_exe_name, exe_data, 0755);
  if (!lib_path || !exe_path) {
    free(lib_path);
    free(exe_path);
    return NULL;
  }
  setenv(k_profiler_ddprof_exe_env_variable, exe_path, 1);
  free(exe_path);
  void *handle = my_dlopen(lib_path, RTLD_LOCAL | RTLD_NOW);
  free(lib_path);
  return handle;
}

static void __attribute__((constructor)) loader() {
  ensure_libdl_is_loaded();
  ensure_libm_is_loaded();
  ensure_libpthread_is_loaded();
  ensure_librt_is_loaded();
  ensure_loader_symbols_promoted();

  const char *lib_profiling_path = getenv(k_profiler_lib_env_variable);
  if (lib_profiling_path) {
    s_profiling_lib_handle =
        my_dlopen(lib_profiling_path, RTLD_LOCAL | RTLD_NOW);
  } else {
    // Check for the exe and an installed lib (with matching hash) before
    // dlopen.  Both must exist; otherwise fall back to embedded extraction.
    char *exe_path = find_ddprof_exe();
    if (exe_path) {
      char *lib_path = find_installed_profiling_lib();
      if (lib_path) {
        setenv(k_profiler_ddprof_exe_env_variable, exe_path, 1);
        void *handle = my_dlopen(lib_path, RTLD_LOCAL | RTLD_NOW);
        if (handle) {
          s_profiling_lib_handle = handle;
        } else {
          fprintf(stderr,
                  "ddprof: failed to dlopen installed %s, falling back to "
                  "embedded library\n",
                  lib_path);
          unsetenv(k_profiler_ddprof_exe_env_variable);
        }
        free(lib_path);
      }
      free(exe_path);
    }
    if (!s_profiling_lib_handle) {
      s_profiling_lib_handle = load_embedded_profiling_lib();
    }
  }

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
