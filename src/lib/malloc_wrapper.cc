// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <dlfcn.h>
#include <malloc.h>

#include "allocation_tracker.hpp"
#include "unlikely.h"

// Declaration of reallocarray is only available starting from glibc 2.28
extern "C" {
void *reallocarray(void *ptr, size_t nmemb, size_t size) noexcept;

void *temp_malloc(size_t size) noexcept;
void temp_free(void *ptr) noexcept;
void *temp_calloc(size_t nmemb, size_t size) noexcept;
void *temp_realloc(void *ptr, size_t size) noexcept;
int temp_posix_memalign(void **memptr, size_t alignment, size_t size) noexcept;
void *temp_aligned_alloc(size_t alignment, size_t size) noexcept;
void *temp_memalign(size_t alignment, size_t size) noexcept;
void *temp_pvalloc(size_t size) noexcept;
void *temp_valloc(size_t size) noexcept;
void *temp_reallocarray(void *ptr, size_t nmemb, size_t size) noexcept;
}

#define ORIGINAL_FUNC(name) get_next<decltype(&::name)>(#name)
#define DECLARE_FUNC(name) decltype(&::name) s_##name = &temp_##name;

template <typename F> F get_next(const char *name) {
  auto *func = reinterpret_cast<F>(dlsym(RTLD_NEXT, name));
  return func;
}

DECLARE_FUNC(malloc);
DECLARE_FUNC(calloc);
DECLARE_FUNC(realloc);
DECLARE_FUNC(free);
DECLARE_FUNC(posix_memalign);
DECLARE_FUNC(aligned_alloc);
DECLARE_FUNC(reallocarray);

// obsolete allocation functions
DECLARE_FUNC(memalign);
DECLARE_FUNC(pvalloc);
DECLARE_FUNC(valloc);

namespace {
void init() __attribute__((noinline));

// calloc is invoked by dlsym, returning a null value in this case is well
// handled by glibc
void *temp_calloc2(size_t, size_t) noexcept { return nullptr; }

inline __attribute__((no_sanitize("address"))) void check_init() {
  [[maybe_unused]] static bool init_once = []() {
    init();
    return true;
  }();
}

void init() {
  s_calloc = &temp_calloc2;

  s_calloc = ORIGINAL_FUNC(calloc);
  s_malloc = ORIGINAL_FUNC(malloc);
  s_free = ORIGINAL_FUNC(free);
  s_realloc = ORIGINAL_FUNC(realloc);
  s_posix_memalign = ORIGINAL_FUNC(posix_memalign);
  s_aligned_alloc = ORIGINAL_FUNC(aligned_alloc);
  s_memalign = ORIGINAL_FUNC(memalign);
  s_pvalloc = ORIGINAL_FUNC(pvalloc);
  s_valloc = ORIGINAL_FUNC(valloc);
  s_reallocarray = ORIGINAL_FUNC(reallocarray);
}

} // namespace

void *malloc(size_t size) {
  void *ptr = s_malloc(size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size);
  return ptr;
}

void *temp_malloc(size_t size) noexcept {
  check_init();
  return malloc(size);
}

void free(void *ptr) {
  if (ptr == nullptr) {
    return;
  }

  ddprof::track_deallocation(reinterpret_cast<uintptr_t>(ptr));
  s_free(ptr);
}

void temp_free(void *ptr) noexcept {
  check_init();
  return free(ptr);
}

void *calloc(size_t nmemb, size_t size) {
  void *ptr = s_calloc(nmemb, size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size * nmemb);
  return ptr;
}

void *temp_calloc(size_t nmemb, size_t size) noexcept {
  check_init();
  return calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size) {
  if (ptr) {
    ddprof::track_deallocation(reinterpret_cast<uintptr_t>(ptr));
  }
  void *newptr = s_realloc(ptr, size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size);
  return newptr;
}

void *temp_realloc(void *ptr, size_t size) noexcept {
  check_init();
  return realloc(ptr, size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
  int ret = s_posix_memalign(memptr, alignment, size);
  if (likely(!ret)) {
    ddprof::track_allocation(reinterpret_cast<uintptr_t>(*memptr), size);
  }
  return ret;
}

int temp_posix_memalign(void **memptr, size_t alignment, size_t size) noexcept {
  check_init();
  return posix_memalign(memptr, alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size) {
  void *ptr = s_aligned_alloc(alignment, size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size);
  return ptr;
}

void *temp_aligned_alloc(size_t alignment, size_t size) noexcept {
  check_init();
  return aligned_alloc(alignment, size);
}

void *memalign(size_t alignment, size_t size) {
  void *ptr = s_memalign(alignment, size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size);
  return ptr;
}
void *temp_memalign(size_t alignment, size_t size) noexcept {
  check_init();
  return memalign(alignment, size);
}

void *pvalloc(size_t size) {
  void *ptr = s_pvalloc(size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size);
  return ptr;
}

void *temp_pvalloc(size_t size) noexcept {
  check_init();
  return pvalloc(size);
}

void *valloc(size_t size) {
  void *ptr = s_valloc(size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size);
  return ptr;
}

void *temp_valloc(size_t size) noexcept {
  check_init();
  return valloc(size);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) noexcept {
  if (ptr) {
    ddprof::track_deallocation(reinterpret_cast<uintptr_t>(ptr));
  }
  void *newptr = s_reallocarray(ptr, nmemb, size);
  ddprof::track_allocation(reinterpret_cast<uintptr_t>(ptr), size * nmemb);
  return newptr;
}

void *temp_reallocarray(void *ptr, size_t nmemb, size_t size) noexcept {
  check_init();
  return reallocarray(ptr, nmemb, size);
}