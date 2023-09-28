// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pthread_fixes.hpp"

/* Some public libc structs don't have the same size between glibc and musl.
 * On amd64, only `regoff_t` (used for regex API) has a different size ( 4 on
 * glibc vs 8 on musl), size on glibc appears to depend on some
 * [macro](https://github.com/gagern/gnulib/blob/master/lib/regex.h#L488).
 * Since we don't use regex, this difference is not an issue.
 *
 * On arm64, on top of `regoff_t`, a bunch of struct related to pthreads have
 * different sizes:
 *  - pthread_attr_t: 64 vs 56
 *  - pthread_barrierattr_t: 8 vs 4
 *  - pthread_cond_attr_t: 8 vs 4
 *  - pthread_mutex_t: 48 vs 40
 *  - pthread_mutexattr_t: 8 vs 4
 *  - mtx_t: 48 vs 40
 * This is much more worrisome because space allocated at compile time for a
 * member in a struct or for a variable on stack might be smaller than the space
 * used at runtime by pthread functions consuming these types.
 * For example, all init functions (such as pthread_attr_init /
 * pthread_mutex_init / ...) when invoked in glibc with code compiled in musl
 * will result in out-of-bounds writes when they memset their parameters to 0.
 * That's also the case for pthread_getattr_np that calls pthread_attr_init.
 * Luckily it seems that the 8 additional bytes in pthread_attr_t and
 * pthread_mutex_t are not actually used, that makes pthread_mutex_t safe to use
 * if not initialized with pthread_mutex_init.
 *
 * To avoid any issue, we must not use:
 *  - any pthread_xxx_init function
 *  - pthread_getattr_np
 *  - pthread_getattr_default_np / pthread_setattr_default_np
 */

namespace {
union pthread_attr_safe_t {
  pthread_attr_t attrs;

  // extra size to match glibc size
  // cppcheck-suppress unusedStructMember
  char reserved[64]; // NOLINT(readability-magic-numbers)
};
} // namespace

// Safe version of pthread_getattr_np
int pthread_getattr_np_safe(pthread_t th, pthread_attr_t *attr) {
  // pad pthread_getattr_np argument with extra space to avoid out-of-bound
  // write on the stack
  pthread_attr_safe_t safe_attrs;
  int const res = pthread_getattr_np(th, &safe_attrs.attrs);
  if (!res) {
    *attr = safe_attrs.attrs;
  }
  return res;
}
