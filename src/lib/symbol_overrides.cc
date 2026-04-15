// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#ifdef MUSL_LIBC
// Required to get mmap64 declaration:
// https://wiki.musl-libc.org/faq#Q:-Do-I-need-to-define-%3Ccode%3E_LARGEFILE64_SOURCE%3C/code%3E-to-get-64bit-%3Ccode%3Eoff_t%3C/code%3E?
#  define _LARGEFILE64_SOURCE
#endif
#include "symbol_overrides.hpp"

#include "allocation_tracker.hpp"
#include "ddprof_base.hpp"
#include "sampled_allocation_marker.hpp"
#include "elfutils.hpp"
#include "logger.hpp"
#include "reentry_guard.hpp"
#include "unlikely.hpp"

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <malloc.h>
#include <sys/mman.h>

#if defined(__GNUC__) && !defined(__clang__)
#  define NOEXCEPT noexcept
#else
#  define NOEXCEPT
#endif

extern "C" {
// NOLINTBEGIN
// Declaration of reallocarray is only available starting from glibc 2.28
DDPROF_WEAK void *reallocarray(void *ptr, size_t nmemb, size_t nmenb) NOEXCEPT;
DDPROF_WEAK void *pvalloc(size_t size) NOEXCEPT;
DDPROF_WEAK int __libc_allocate_rtsig(int high) NOEXCEPT;
// NOLINTEND

// sized free functions (C23, not yet available in glibc)
DDPROF_WEAK void free_sized(void *ptr, size_t size);
DDPROF_WEAK void free_aligned_sized(void *ptr, size_t alignment, size_t size);

// jemalloc Non-standard API
DDPROF_WEAK void *mallocx(size_t size, int flags);
DDPROF_WEAK void *rallocx(void *ptr, size_t size, int flags);
DDPROF_WEAK size_t xallocx(void *ptr, size_t size, size_t extra, int flags);
DDPROF_WEAK void dallocx(void *ptr, int flags);
DDPROF_WEAK void sdallocx(void *ptr, size_t size, int flags);
}

namespace ddprof {

namespace {

std::unique_ptr<SymbolOverrides> g_symbol_overrides;
std::mutex g_mutex;

class MaybeReentryGuard {
public:
  MaybeReentryGuard()
      : _tl_state{ddprof::AllocationTracker::get_tl_state()},
        _guard{_tl_state ? &(_tl_state->reentry_guard) : nullptr} {}

  explicit operator bool() const { return static_cast<bool>(_guard); }
  ddprof::TrackerThreadLocalState *tl_state() { return _tl_state; }

private:
  ddprof::TrackerThreadLocalState *_tl_state;
  ddprof::ReentryGuard _guard;
};

template <bool mmap_alloc> class AllocTrackerHelperImpl {
public:
  // Do not try to initialize the TLS state when intercepting
  // mmap/mmap64/munmap/munmap_ because those functions might be called from
  // malloc implementation and cause a deadlock when pthread_getattr_np (which
  // can call malloc) is called by tls state initialization.
  AllocTrackerHelperImpl()
      : _tl_state{ddprof::AllocationTracker::get_tl_state(!mmap_alloc)},
        _guard{_tl_state ? &(_tl_state->reentry_guard) : nullptr} {}

  void track(void *ptr, size_t size) {
    if (_guard) {
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = false;
      }
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *_tl_state, mmap_alloc);
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = true;
      }
    }
  }

#if defined(__aarch64__)
  // ARM64: track and return whether the allocation was sampled.
  bool track_sampled(void *ptr, size_t size) {
    if (_guard) {
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = false;
      }
      bool sampled = ddprof::AllocationTracker::track_allocation_s_sampled(
          reinterpret_cast<uintptr_t>(ptr), size, *_tl_state, mmap_alloc);
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = true;
      }
      return sampled;
    }
    return false;
  }
#elif defined(__x86_64__)
  // AMD64: pre-check whether the next allocation will be sampled (non-mmap).
  bool will_sample(size_t size) {
    if (_guard) {
      return ddprof::AllocationTracker::will_sample(size, *_tl_state);
    }
    return false;
  }
#endif

  explicit operator bool() const { return static_cast<bool>(_guard); }
  ddprof::TrackerThreadLocalState *tl_state() { return _tl_state; }

private:
  ddprof::TrackerThreadLocalState *_tl_state;
  ddprof::ReentryGuard _guard;
};

template <bool mmap_alloc = false> class DeallocTrackerHelperImpl {
public:
  DeallocTrackerHelperImpl()
      : _tl_state{ddprof::AllocationTracker::is_deallocation_tracking_active()
                      ? ddprof::AllocationTracker::get_tl_state(!mmap_alloc)
                      : nullptr},
        _guard{_tl_state ? &(_tl_state->reentry_guard) : nullptr} {}

  void track(void *ptr) {
    if (_guard) {
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = false;
      }
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *_tl_state, mmap_alloc);
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = true;
      }
    }
  }

  // Bypass bitset -- called when marker already confirmed this was sampled.
  void track_direct(void *ptr) {
    if (_guard) {
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = false;
      }
      ddprof::AllocationTracker::track_deallocation_direct_s(
          reinterpret_cast<uintptr_t>(ptr), *_tl_state);
      if constexpr (mmap_alloc) {
        _tl_state->allocation_allowed = true;
      }
    }
  }

  explicit operator bool() const { return static_cast<bool>(_guard); }
  ddprof::TrackerThreadLocalState *tl_state() { return _tl_state; }

private:
  ddprof::TrackerThreadLocalState *_tl_state;
  ddprof::ReentryGuard _guard;
};

using AllocTrackerHelper = AllocTrackerHelperImpl<false>;
using DeallocTrackerHelper = DeallocTrackerHelperImpl<false>;

using AllocTrackerHelperMmap = AllocTrackerHelperImpl<true>;
using DeallocTrackerHelperMmap = DeallocTrackerHelperImpl<true>;

#if defined(__x86_64__)
// Helper for AMD64 free/delete paths: check for prefix marker and handle
// deallocation tracking. Returns true if the pointer was prefixed (caller
// should NOT call the original free -- it was already freed via orig_free).
// Page-aligned pointers are skipped to avoid reading before unmapped pages
// (e.g. large mmap'd allocations). Page-aligned allocations (pvalloc/valloc)
// use the bitset path instead of the prefix approach.
template <typename FreeFn>
bool free_with_prefix_check(void *ptr, FreeFn orig_free) {
  if (!ddprof::marker::is_page_aligned(ptr)) {
    auto [found, original] = ddprof::marker::read_prefix(ptr);
    if (found) {
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
      orig_free(original);
      return true;
    }
  }
  return false;
}
#endif

struct HookBase {};

struct MallocHook : HookBase {
  static constexpr auto name = "malloc";
  using FuncType = decltype(&::malloc);
  static inline FuncType ref{};

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      ptr = ref(size + ddprof::marker::kMinPrefixSize);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, 16);
      }
    } else {
      ptr = ref(size);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewHook : HookBase {
  static constexpr auto name = "_Znwm";
  using FuncType = decltype(static_cast<void *(*)(size_t)>(&::operator new));
  static inline FuncType ref{};

  static void *hook(size_t size) {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      ptr = ref(size + ddprof::marker::kMinPrefixSize);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, 16);
      }
    } else {
      ptr = ref(size);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewNoThrowHook : HookBase {
  static constexpr auto name = "_ZnwmRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void *(*)(size_t, const std::nothrow_t &) noexcept>(
          &::operator new));
  static inline FuncType ref{};

  static void *hook(size_t size, const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, tag);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      ptr = ref(size + ddprof::marker::kMinPrefixSize, tag);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, 16);
      }
    } else {
      ptr = ref(size, tag);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewAlignHook : HookBase {
  static constexpr auto name = "_ZnwmSt11align_val_t";
  using FuncType = decltype(static_cast<void *(*)(size_t, std::align_val_t)>(
      &::operator new));
  static inline FuncType ref{};

  static void *hook(std::size_t size, std::align_val_t al) {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, al);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    auto alignment = static_cast<size_t>(al);
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(size + psize, al);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(size, al);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZnwmSt11align_val_tRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void *(*)(size_t, std::align_val_t,
                                     const std::nothrow_t &) noexcept>(
          &::operator new));
  static inline FuncType ref{};

  static void *hook(std::size_t size, std::align_val_t al,
                    const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, al, tag);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    auto alignment = static_cast<size_t>(al);
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(size + psize, al, tag);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(size, al, tag);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewArrayHook : HookBase {
  static constexpr auto name = "_Znam";
  using FuncType = decltype(static_cast<void *(*)(size_t)>(&::operator new[]));
  static inline FuncType ref{};

  static void *hook(size_t size) {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      ptr = ref(size + ddprof::marker::kMinPrefixSize);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, 16);
      }
    } else {
      ptr = ref(size);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewArrayNoThrowHook : HookBase {
  static constexpr auto name = "_ZnamRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void *(*)(size_t, const std::nothrow_t &) noexcept>(
          &::operator new[]));
  static inline FuncType ref{};

  static void *hook(size_t size, const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, tag);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      ptr = ref(size + ddprof::marker::kMinPrefixSize, tag);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, 16);
      }
    } else {
      ptr = ref(size, tag);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewArrayAlignHook : HookBase {
  static constexpr auto name = "_ZnamSt11align_val_t";
  using FuncType = decltype(static_cast<void *(*)(size_t, std::align_val_t)>(
      &::operator new[]));
  static inline FuncType ref{};

  static void *hook(std::size_t size, std::align_val_t al) {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, al);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    auto alignment = static_cast<size_t>(al);
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(size + psize, al);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(size, al);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct NewArrayAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZnamSt11align_val_tRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void *(*)(size_t, std::align_val_t,
                                     const std::nothrow_t &) noexcept>(
          &::operator new[]));
  static inline FuncType ref{};

  static void *hook(std::size_t size, std::align_val_t al,
                    const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, al, tag);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    auto alignment = static_cast<size_t>(al);
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(size + psize, al, tag);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(size, al, tag);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct FreeHook : HookBase {
  static constexpr auto name = "free";
  using FuncType = decltype(&::free);
  static inline FuncType ref{};

  static void hook(void *ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, ref)) {
      return;
    }
    // Page-aligned pointers may be from pvalloc/valloc (tracked via bitset
    // with is_large_alloc=true) or mmap (also is_large_alloc=true).
    // Non-page-aligned non-prefixed pointers were not sampled.
    if (ddprof::marker::is_page_aligned(ptr)) {
      DeallocTrackerHelperMmap helper;
      helper.track(ptr);
    } else {
      DeallocTrackerHelper helper;
      helper.track(ptr);
    }
    ref(ptr);
#endif
  }
};

struct FreeSizedHook : HookBase {
  static constexpr auto name = "free_sized";
  using FuncType = decltype(&::free_sized);
  static inline FuncType ref{};

  static void hook(void *ptr, size_t size) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, size);
#elif defined(__x86_64__)
    // For prefixed allocs, fall back to regular free (exact size unknown)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, size);
#endif
  }
};

struct FreeAlignedSizedHook : HookBase {
  static constexpr auto name = "free_aligned_sized";
  using FuncType = decltype(&::free_aligned_sized);
  static inline FuncType ref{};

  static void hook(void *ptr, size_t alignment, size_t size) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, alignment, size);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, alignment, size);
#endif
  }
};

struct DeleteHook : HookBase {
  static constexpr auto name = "_ZdlPv";
  using FuncType = decltype(static_cast<void (*)(void *)>(&::operator delete));
  static inline FuncType ref{};

  static void hook(void *ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr);
#endif
  }
};

struct DeleteArrayHook : HookBase {
  static constexpr auto name = "_ZdaPv";
  using FuncType =
      decltype(static_cast<void (*)(void *)>(&::operator delete[]));
  static inline FuncType ref{};

  static void hook(void *ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr);
#endif
  }
};

struct DeleteNoThrowHook : HookBase {
  static constexpr auto name = "_ZdlPvRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void (*)(void *, const std::nothrow_t &) noexcept>(
          &::operator delete));
  static inline FuncType ref{};

  static void hook(void *ptr, const std::nothrow_t &tag) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, tag);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, tag);
#endif
  }
};

struct DeleteArrayNoThrowHook : HookBase {
  static constexpr auto name = "_ZdaPvRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void (*)(void *, const std::nothrow_t &) noexcept>(
          &::operator delete[]));
  static inline FuncType ref{};

  static void hook(void *ptr, const std::nothrow_t &tag) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, tag);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, tag);
#endif
  }
};

struct DeleteAlignHook : HookBase {
  static constexpr auto name = "_ZdlPvSt11align_val_t";
  using FuncType =
      decltype(static_cast<void (*)(void *, std::align_val_t) noexcept>(
          &::operator delete));
  static inline FuncType ref{};

  static void hook(void *ptr, std::align_val_t al) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, al);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, al);
#endif
  }
};

struct DeleteArrayAlignHook : HookBase {
  static constexpr auto name = "_ZdaPvSt11align_val_t";
  using FuncType =
      decltype(static_cast<void (*)(void *, std::align_val_t) noexcept>(
          &::operator delete[]));
  static inline FuncType ref{};

  static void hook(void *ptr, std::align_val_t al) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, al);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, al);
#endif
  }
};

struct DeleteAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZdlPvSt11align_val_tRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void (*)(void *, std::align_val_t,
                                    const std::nothrow_t &) noexcept>(
          &::operator delete));
  static inline FuncType ref{};

  static void hook(void *ptr, std::align_val_t al,
                   const std::nothrow_t &tag) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, al, tag);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, al, tag);
#endif
  }
};

struct DeleteArrayAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZdaPvSt11align_val_tRKSt9nothrow_t";
  using FuncType =
      decltype(static_cast<void (*)(void *, std::align_val_t,
                                    const std::nothrow_t &) noexcept>(
          &::operator delete[]));
  static inline FuncType ref{};

  static void hook(void *ptr, std::align_val_t al,
                   const std::nothrow_t &tag) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, al, tag);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, al, tag);
#endif
  }
};

struct DeleteSizedHook : HookBase {
  static constexpr auto name = "_ZdlPvm";
  using FuncType = decltype(static_cast<void (*)(void *, std::size_t) noexcept>(
      &::operator delete));
  static inline FuncType ref{};

  static void hook(void *ptr, std::size_t size) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, size);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, size);
#endif
  }
};

struct DeleteArraySizedHook : HookBase {
  static constexpr auto name = "_ZdaPvm";
  using FuncType = decltype(static_cast<void (*)(void *, std::size_t) noexcept>(
      &::operator delete[]));
  static inline FuncType ref{};

  static void hook(void *ptr, std::size_t size) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, size);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, size);
#endif
  }
};

struct DeleteSizedAlignHook : HookBase {
  static constexpr auto name = "_ZdlPvmSt11align_val_t";
  using FuncType = decltype(static_cast<void (*)(void *, std::size_t,
                                                 std::align_val_t) noexcept>(
      &::operator delete));
  static inline FuncType ref{};

  static void hook(void *ptr, std::size_t size, std::align_val_t al) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, size, al);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, size, al);
#endif
  }
};

struct DeleteArraySizedAlignHook : HookBase {
  static constexpr auto name = "_ZdaPvmSt11align_val_t";
  using FuncType = decltype(static_cast<void (*)(void *, std::size_t,
                                                 std::align_val_t) noexcept>(
      &::operator delete[]));
  static inline FuncType ref{};

  static void hook(void *ptr, std::size_t size, std::align_val_t al) noexcept {
    if (ptr == nullptr) {
      return;
    }
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, size, al);
#elif defined(__x86_64__)
    if (free_with_prefix_check(ptr, FreeHook::ref)) {
      return;
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, size, al);
#endif
  }
};

struct CallocHook : HookBase {
  static constexpr auto name = "calloc";
  using FuncType = decltype(&::calloc);
  static inline FuncType ref{};

  static void *hook(size_t nmemb, size_t size) noexcept {
    AllocTrackerHelper helper;
    size_t total = size * nmemb;
#if defined(__aarch64__)
    auto *ptr = ref(nmemb, size);
    if (helper.track_sampled(ptr, total)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(total);
    void *ptr;
    if (ws) {
      // calloc zeroes memory; call with adjusted size, then write prefix
      // over the zeroed prefix area. User data area remains properly zeroed.
      ptr = ref(1, total + ddprof::marker::kMinPrefixSize);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, 16);
      }
    } else {
      ptr = ref(nmemb, size);
    }
    helper.track(ptr, total);
    return ptr;
#endif
  }
};

struct ReallocHook : HookBase {
  static constexpr auto name = "realloc";
  using FuncType = decltype(&::realloc);
  static inline FuncType ref{};

  static void *hook(void *ptr, size_t size) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    void *real_ptr = ptr;
    if (ptr && ddprof::marker::is_tagged(ptr)) {
      real_ptr = ddprof::marker::untag(ptr);
      if (helper) {
        ddprof::AllocationTracker::track_deallocation_direct_s(
            reinterpret_cast<uintptr_t>(real_ptr), *helper.tl_state());
      }
    } else if (likely(ptr) && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto *newptr = ref(real_ptr, size);
    if (likely(size) && helper.track_sampled(newptr, size)) {
      newptr = ddprof::marker::tag(newptr);
    }
    return newptr;
#elif defined(__x86_64__)
    void *real_ptr = ptr;
    bool was_prefixed = false;
    if (ptr && !ddprof::marker::is_page_aligned(ptr)) {
      auto [found, original] = ddprof::marker::read_prefix(ptr);
      if (found) {
        was_prefixed = true;
        real_ptr = original;
        if (helper) {
          ddprof::AllocationTracker::track_deallocation_direct_s(
              reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
        }
      }
    }
    if (!was_prefixed && likely(ptr) && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    bool ws = helper.will_sample(size);
    void *newptr;
    if (ws) {
      newptr = ref(real_ptr, size + ddprof::marker::kMinPrefixSize);
      if (newptr) {
        newptr = ddprof::marker::write_prefix(newptr, 16);
      }
    } else {
      newptr = ref(real_ptr, size);
    }
    if (likely(size)) {
      helper.track(newptr, size);
    }
    return newptr;
#endif
  }
};

struct PosixMemalignHook : HookBase {
  static constexpr auto name = "posix_memalign";
  using FuncType = decltype(&::posix_memalign);
  static inline FuncType ref{};

  static int hook(void **memptr, size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto ret = ref(memptr, alignment, size);
    if (likely(!ret)) {
      if (helper.track_sampled(*memptr, size)) {
        *memptr = ddprof::marker::tag(*memptr);
      }
    }
    return ret;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      auto ret = ref(memptr, alignment, size + psize);
      if (likely(!ret) && *memptr) {
        *memptr = ddprof::marker::write_prefix(*memptr, alignment);
      }
      if (likely(!ret)) {
        helper.track(*memptr, size);
      }
      return ret;
    }
    auto ret = ref(memptr, alignment, size);
    if (likely(!ret)) {
      helper.track(*memptr, size);
    }
    return ret;
#endif
  }
};

struct AlignedAllocHook : HookBase {
  static constexpr auto name = "aligned_alloc";
  using FuncType = decltype(&::aligned_alloc);
  static inline FuncType ref{};

  static void *hook(size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(alignment, size);
    if (ptr && helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(alignment, size + psize);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(alignment, size);
    }
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
#endif
  }
};

struct MemalignHook : HookBase {
  static constexpr auto name = "memalign";
  using FuncType = decltype(&::memalign);
  static inline FuncType ref{};

  static void *hook(size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(alignment, size);
    if (ptr && helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(alignment, size + psize);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(alignment, size);
    }
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
#endif
  }
};

struct PvallocHook : HookBase {
  static constexpr auto name = "pvalloc";
  using FuncType = decltype(&::pvalloc);
  static inline FuncType ref{};

  static void *hook(size_t size) noexcept {
    // pvalloc returns page-aligned memory. On AMD64, page-aligned pointers
    // cannot use the prefix approach (the is_page_aligned check in free would
    // skip them). Use the mmap/bitset tracking path instead.
#if defined(__aarch64__)
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    if (ptr && helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    AllocTrackerHelperMmap helper;
    auto *ptr = ref(size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
#endif
  }
};

struct VallocHook : HookBase {
  static constexpr auto name = "valloc";
  using FuncType = decltype(&::valloc);
  static inline FuncType ref{};

  static void *hook(size_t size) noexcept {
    // valloc returns page-aligned memory. Same approach as pvalloc.
#if defined(__aarch64__)
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    if (ptr && helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    AllocTrackerHelperMmap helper;
    auto *ptr = ref(size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
#endif
  }
};

struct ReallocArrayHook : HookBase {
  static constexpr auto name = "reallocarray";
  using FuncType = decltype(&::reallocarray);
  static inline FuncType ref{};

  static void *hook(void *ptr, size_t nmemb, size_t size) noexcept {
    AllocTrackerHelper helper;
    size_t total = size * nmemb;
#if defined(__aarch64__)
    void *real_ptr = ptr;
    if (ptr && ddprof::marker::is_tagged(ptr)) {
      real_ptr = ddprof::marker::untag(ptr);
      if (helper) {
        ddprof::AllocationTracker::track_deallocation_direct_s(
            reinterpret_cast<uintptr_t>(real_ptr), *helper.tl_state());
      }
    } else if (ptr && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto *newptr = ref(real_ptr, nmemb, size);
    if (newptr && helper.track_sampled(newptr, total)) {
      newptr = ddprof::marker::tag(newptr);
    }
    return newptr;
#elif defined(__x86_64__)
    void *real_ptr = ptr;
    bool was_prefixed = false;
    if (ptr && !ddprof::marker::is_page_aligned(ptr)) {
      auto [found, original] = ddprof::marker::read_prefix(ptr);
      if (found) {
        was_prefixed = true;
        real_ptr = original;
        if (helper) {
          ddprof::AllocationTracker::track_deallocation_direct_s(
              reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
        }
      }
    }
    if (!was_prefixed && ptr && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    bool ws = helper.will_sample(total);
    void *newptr;
    if (ws) {
      newptr = ref(real_ptr, 1, total + ddprof::marker::kMinPrefixSize);
      if (newptr) {
        newptr = ddprof::marker::write_prefix(newptr, 16);
      }
    } else {
      newptr = ref(real_ptr, nmemb, size);
    }
    if (newptr) {
      helper.track(newptr, total);
    }
    return newptr;
#endif
  }
};

struct DlopenHook : HookBase {
  static constexpr auto name = "dlopen";
  using FuncType = decltype(&::dlopen);
  static inline FuncType ref{};

  static void *hook(const char *filename, int flags) noexcept {
    void *ret = ref(filename, flags);
    update_overrides();
    return ret;
  }
};

struct MallocxHook : HookBase {
  static constexpr auto name = "mallocx";
  using FuncType = decltype(&::mallocx);
  static inline FuncType ref{};

  static void *hook(size_t size, int flags) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    auto *ptr = ref(size, flags);
    if (helper.track_sampled(ptr, size)) {
      ptr = ddprof::marker::tag(ptr);
    }
    return ptr;
#elif defined(__x86_64__)
    bool ws = helper.will_sample(size);
    void *ptr;
    if (ws) {
      // Extract alignment from jemalloc flags (MALLOCX_LG_ALIGN_MASK = 0x3f)
      size_t alignment = 16;
      if (flags & 0x3f) {
        alignment =
            std::max(alignment, static_cast<size_t>(1) << (flags & 0x3f));
      }
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      ptr = ref(size + psize, flags);
      if (ptr) {
        ptr = ddprof::marker::write_prefix(ptr, alignment);
      }
    } else {
      ptr = ref(size, flags);
    }
    helper.track(ptr, size);
    return ptr;
#endif
  }
};

struct RallocxHook : HookBase {
  static constexpr auto name = "rallocx";
  using FuncType = decltype(&::rallocx);
  static inline FuncType ref{};

  static void *hook(void *ptr, size_t size, int flags) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    void *real_ptr = ptr;
    if (ptr && ddprof::marker::is_tagged(ptr)) {
      real_ptr = ddprof::marker::untag(ptr);
      if (helper) {
        ddprof::AllocationTracker::track_deallocation_direct_s(
            reinterpret_cast<uintptr_t>(real_ptr), *helper.tl_state());
      }
    } else if (likely(ptr) && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto *newptr = ref(real_ptr, size, flags);
    if (likely(size) && helper.track_sampled(newptr, size)) {
      newptr = ddprof::marker::tag(newptr);
    }
    return newptr;
#elif defined(__x86_64__)
    void *real_ptr = ptr;
    bool was_prefixed = false;
    if (likely(ptr) && !ddprof::marker::is_page_aligned(ptr)) {
      auto [found, original] = ddprof::marker::read_prefix(ptr);
      if (found) {
        was_prefixed = true;
        real_ptr = original;
        if (helper) {
          ddprof::AllocationTracker::track_deallocation_direct_s(
              reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
        }
      }
    }
    if (!was_prefixed && likely(ptr) && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    bool ws = helper.will_sample(size);
    size_t alignment = 16;
    if (flags & 0x3f) {
      alignment =
          std::max(alignment, static_cast<size_t>(1) << (flags & 0x3f));
    }
    void *newptr;
    if (ws) {
      size_t psize = ddprof::marker::prefix_size_for_alignment(alignment);
      newptr = ref(real_ptr, size + psize, flags);
      if (newptr) {
        newptr = ddprof::marker::write_prefix(newptr, alignment);
      }
    } else {
      newptr = ref(real_ptr, size, flags);
    }
    if (likely(size)) {
      helper.track(newptr, size);
    }
    return newptr;
#endif
  }
};

struct XallocxHook : HookBase {
  static constexpr auto name = "xallocx";
  using FuncType = decltype(&::xallocx);
  static inline FuncType ref{};

  static size_t hook(void *ptr, size_t size, size_t extra, int flags) noexcept {
    AllocTrackerHelper helper;
#if defined(__aarch64__)
    void *real_ptr = ptr;
    if (ddprof::marker::is_tagged(ptr)) {
      real_ptr = ddprof::marker::untag(ptr);
    }
    if (helper) {
      ddprof::AllocationTracker::track_deallocation_direct_s(
          reinterpret_cast<uintptr_t>(real_ptr), *helper.tl_state());
    }
    auto newsize = ref(real_ptr, size, extra, flags);
    helper.track(real_ptr, newsize);
    return newsize;
#elif defined(__x86_64__)
    void *real_ptr = ptr;
    size_t psize = 0;
    bool was_prefixed = false;
    if (!ddprof::marker::is_page_aligned(ptr)) {
      auto [found, original] = ddprof::marker::read_prefix(ptr);
      if (found) {
        was_prefixed = true;
        real_ptr = original;
        psize = ddprof::marker::kMinPrefixSize;
      }
    }
    if (helper) {
      if (was_prefixed) {
        ddprof::AllocationTracker::track_deallocation_direct_s(
            reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
      } else {
        ddprof::AllocationTracker::track_deallocation_s(
            reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
      }
    }
    auto newsize = ref(real_ptr, size + psize, extra, flags);
    auto effective_size = psize ? newsize - psize : newsize;
    helper.track(ptr, effective_size);
    return effective_size;
#endif
  }
};

struct DallocxHook : HookBase {
  static constexpr auto name = "dallocx";
  using FuncType = decltype(&::dallocx);
  static inline FuncType ref{};

  static void hook(void *ptr, int flags) noexcept {
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, flags);
#elif defined(__x86_64__)
    if (!ddprof::marker::is_page_aligned(ptr)) {
      auto [found, original] = ddprof::marker::read_prefix(ptr);
      if (found) {
        DeallocTrackerHelper helper;
        helper.track_direct(ptr);
        ref(original, flags);
        return;
      }
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, flags);
#endif
  }
};

struct SdallocxHook : HookBase {
  static constexpr auto name = "sdallocx";
  using FuncType = decltype(&::sdallocx);
  static inline FuncType ref{};

  static void hook(void *ptr, size_t size, int flags) noexcept {
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(ptr)) {
      ptr = ddprof::marker::untag(ptr);
      DeallocTrackerHelper helper;
      helper.track_direct(ptr);
    }
    ref(ptr, size, flags);
#elif defined(__x86_64__)
    if (!ddprof::marker::is_page_aligned(ptr)) {
      auto [found, original] = ddprof::marker::read_prefix(ptr);
      if (found) {
        DeallocTrackerHelper helper;
        helper.track_direct(ptr);
        // Use dallocx since we don't know the real size
        DallocxHook::ref(original, flags);
        return;
      }
    }
    DeallocTrackerHelper helper;
    helper.track(ptr);
    ref(ptr, size, flags);
#endif
  }
};

using Args = std::tuple<void *(*)(void *), void *>;

void *my_start(void *arg) {
  ddprof::AllocationTracker::notify_thread_start();
  Args *args = reinterpret_cast<Args *>(arg);
  auto [start_routine, start_arg] = *args;
  delete args;
  return start_routine(start_arg);
}

/** Hook pthread_create to cache stack end address just after thread start.
 *
 * The rationale is to fix a deadlock that occurs when user code in created
 * thread calls pthread_getattr:
 * - pthread_getattr takes a lock in pthread object
 * - pthread_getattr itself does an allocation
 * - AllocationTracker tracks the allocation and calls save_context
 * - save_context calls pthread_getattr to get stack end address
 * - pthread_getattr is reentered and attempts to take the lock again leading to
 * a deadlock.
 *
 * Workaround is to hook pthread_create and call `cache_stack_end` to
 * cache stack end address while temporarily disabling allocation profiling for
 * current thread before calling user code.
 * */
struct PthreadCreateHook : HookBase {
  static constexpr auto name = "pthread_create";
  using FuncType = decltype(&::pthread_create);
  static inline FuncType ref{};

  static int hook(pthread_t *thread, const pthread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg) noexcept {
    Args *args = new (std::nothrow) Args{start_routine, arg};
    return args ? ref(thread, attr, &my_start, args)
                : ref(thread, attr, start_routine, arg);
  }
};

struct PthreadGetattrHook : HookBase {
  static constexpr auto name = "pthread_getattr_np";
  using FuncType = decltype(&::pthread_getattr_np);
  static inline FuncType ref{};

  static int hook(pthread_t thread, pthread_attr_t *attr) noexcept {
    ddprof::AllocationTracker::notify_pthread_getattr_np();
    auto ret = ref(thread, attr);
    ddprof::AllocationTracker::notify_pthread_getattr_np_end();
    return ret;
  }
};

struct MmapHook : HookBase {
  static constexpr auto name = "mmap";
  using FuncType = decltype(&::mmap);
  static inline FuncType ref{};

  static void *hook(void *addr, size_t length, int prot, int flags, int fd,
                    off_t offset) noexcept {
    AllocTrackerHelperMmap helper;
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr) {
#if defined(__aarch64__)
      if (helper.track_sampled(ptr, length)) {
        ptr = ddprof::marker::tag(ptr);
      }
#else
      helper.track(ptr, length);
#endif
    }
    return ptr;
  }
};

struct Mmap_Hook : HookBase {
  static constexpr auto name = "__mmap";
  using FuncType = decltype(&::mmap);
  static inline FuncType ref{};

  static void *hook(void *addr, size_t length, int prot, int flags, int fd,
                    off_t offset) noexcept {
    AllocTrackerHelperMmap helper;
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr) {
#if defined(__aarch64__)
      if (helper.track_sampled(ptr, length)) {
        ptr = ddprof::marker::tag(ptr);
      }
#else
      helper.track(ptr, length);
#endif
    }
    return ptr;
  }
};

struct Mmap64Hook : HookBase {
  static constexpr auto name = "mmap64";
  using FuncType = decltype(&::mmap64);
  static inline FuncType ref{};

  static void *hook(void *addr, size_t length, int prot, int flags, int fd,
                    off_t offset) noexcept {
    AllocTrackerHelperMmap helper;
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr) {
#if defined(__aarch64__)
      if (helper.track_sampled(ptr, length)) {
        ptr = ddprof::marker::tag(ptr);
      }
#else
      helper.track(ptr, length);
#endif
    }
    return ptr;
  }
};

struct MunmapHook : HookBase {
  static constexpr auto name = "munmap";
  using FuncType = decltype(&::munmap);
  static inline FuncType ref{};

  static int hook(void *addr, size_t length) noexcept {
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(addr)) {
      addr = ddprof::marker::untag(addr);
      DeallocTrackerHelperMmap helper;
      helper.track_direct(addr);
    }
    return ref(addr, length);
#else
    DeallocTrackerHelperMmap helper;
    helper.track(addr);
    return ref(addr, length);
#endif
  }
};

struct Munmap_Hook : HookBase {
  static constexpr auto name = "__munmap";
  using FuncType = decltype(&::munmap);
  static inline FuncType ref{};

  static int hook(void *addr, size_t length) noexcept {
#if defined(__aarch64__)
    if (ddprof::marker::is_tagged(addr)) {
      addr = ddprof::marker::untag(addr);
      DeallocTrackerHelperMmap helper;
      helper.track_direct(addr);
    }
    return ref(addr, length);
#else
    DeallocTrackerHelperMmap helper;
    helper.track(addr);
    return ref(addr, length);
#endif
  }
};

template <typename T> void register_hook() {
  g_symbol_overrides->register_override(T::name,
                                        reinterpret_cast<uintptr_t>(&T::hook),
                                        reinterpret_cast<uintptr_t *>(&T::ref));
}

void register_hooks() {
  auto malloc_lookup_result = ddprof::lookup_symbol("malloc", false);
  auto new_lookup_result = ddprof::lookup_symbol(NewHook::name, false);

  // c++ allocators from libstdc++/libc++ calls malloc internally, so we don't
  // need to instrument them. Only instrument them if malloc is not present or
  // if new and malloc are defined in the same library (as what is done in
  // tcmalloc/jemmaloc/mimalloc).
  bool instrument_cxx_allocators = false;
  if (malloc_lookup_result.symbol.st_value == 0 ||
      malloc_lookup_result.symbol.st_size == 0 ||
      (new_lookup_result.symbol.st_value > 0 &&
       new_lookup_result.symbol.st_size > 0 &&
       new_lookup_result.object_name == malloc_lookup_result.object_name)) {
    instrument_cxx_allocators = true;
  }

  LG_DBG("Instrumentation of c++ allocators %s",
         instrument_cxx_allocators ? "enabled" : "disabled");
  register_hook<MallocHook>();
  register_hook<FreeHook>();
  register_hook<FreeSizedHook>();
  register_hook<FreeAlignedSizedHook>();
  register_hook<CallocHook>();
  register_hook<ReallocHook>();
  register_hook<PosixMemalignHook>();
  register_hook<AlignedAllocHook>();
  register_hook<MemalignHook>();
  register_hook<VallocHook>();

  if (instrument_cxx_allocators) {
    register_hook<NewHook>();
    register_hook<NewArrayHook>();
    register_hook<NewNoThrowHook>();
    register_hook<NewArrayNoThrowHook>();
    register_hook<NewAlignHook>();
    register_hook<NewArrayAlignHook>();
    register_hook<NewAlignNoThrowHook>();
    register_hook<NewArrayAlignNoThrowHook>();

    register_hook<DeleteHook>();
    register_hook<DeleteArrayHook>();
    register_hook<DeleteNoThrowHook>();
    register_hook<DeleteArrayNoThrowHook>();
    register_hook<DeleteAlignHook>();
    register_hook<DeleteArrayAlignHook>();
    register_hook<DeleteAlignNoThrowHook>();
    register_hook<DeleteArrayAlignNoThrowHook>();
    register_hook<DeleteSizedHook>();
    register_hook<DeleteArraySizedHook>();
    register_hook<DeleteSizedAlignHook>();
    register_hook<DeleteArraySizedAlignHook>();
  }

  register_hook<MmapHook>();
  register_hook<Mmap64Hook>();
  register_hook<MunmapHook>();
  register_hook<Mmap_Hook>();
  register_hook<Munmap_Hook>();

  register_hook<MallocxHook>();
  register_hook<RallocxHook>();
  register_hook<XallocxHook>();
  register_hook<DallocxHook>();
  register_hook<SdallocxHook>();

  register_hook<ReallocArrayHook>();
  register_hook<PvallocHook>();

  register_hook<PthreadCreateHook>();
  register_hook<DlopenHook>();
  register_hook<PthreadGetattrHook>();
}

} // namespace

void setup_overrides() {
  std::lock_guard const lock(g_mutex);
  MaybeReentryGuard const guard; // avoid tracking allocations

  if (!g_symbol_overrides) {
    g_symbol_overrides = std::make_unique<SymbolOverrides>();
    register_hooks();
  }

  g_symbol_overrides->apply_overrides();
}

void restore_overrides() {
  std::lock_guard const lock(g_mutex);
  MaybeReentryGuard const guard; // avoid tracking allocations

  if (g_symbol_overrides) {
    g_symbol_overrides->restore_overrides();
    g_symbol_overrides.reset();
  }
}

void update_overrides() {
  std::lock_guard const lock(g_mutex);
  MaybeReentryGuard const guard; // avoid tracking allocations

  if (g_symbol_overrides) {
    g_symbol_overrides->update_overrides();
  }
}

} // namespace ddprof
