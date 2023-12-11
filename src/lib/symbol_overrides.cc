// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#define _LARGEFILE64_SOURCE
#include "symbol_overrides.hpp"

#include "allocation_tracker.hpp"
#include "chrono_utils.hpp"
#include "ddprof_base.hpp"
#include "elfutils.hpp"
#include "instrument_function.hpp"
#include "reentry_guard.hpp"
#include "unlikely.hpp"

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <malloc.h>
#include <sys/mman.h>
#include <unordered_map>

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

class AllocTrackerHelper {
public:
  AllocTrackerHelper()
      : _tl_state{ddprof::AllocationTracker::get_tl_state()},
        _guard{_tl_state ? &(_tl_state->reentry_guard) : nullptr} {}

  void track(void *ptr, size_t size) {
    if (_guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *_tl_state);
    }
  }

  explicit operator bool() const { return static_cast<bool>(_guard); }
  ddprof::TrackerThreadLocalState *tl_state() { return _tl_state; }

private:
  ddprof::TrackerThreadLocalState *_tl_state;
  ddprof::ReentryGuard _guard;
};

class DeallocTrackerHelper {
public:
  DeallocTrackerHelper()
      : _tl_state{ddprof::AllocationTracker::is_deallocation_tracking_active()
                      ? ddprof::AllocationTracker::get_tl_state()
                      : nullptr},
        _guard{_tl_state ? &(_tl_state->reentry_guard) : nullptr} {}

  void track(void *ptr) {
    if (_guard) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *_tl_state);
    }
  }

  explicit operator bool() const { return static_cast<bool>(_guard); }
  ddprof::TrackerThreadLocalState *tl_state() { return _tl_state; }

private:
  ddprof::TrackerThreadLocalState *_tl_state;
  ddprof::ReentryGuard _guard;
};

struct HookBase {
  // static inline bool ref_checked = false;
};

struct MallocHook : HookBase {
  static constexpr auto name = "malloc";
  static inline auto ref = &::malloc;

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewHook : HookBase {
  static constexpr auto name = "_Znwm";
  static inline auto ref = static_cast<void *(*)(size_t)>(&::operator new);

  static void *hook(size_t size) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewNoThrowHook : HookBase {
  static constexpr auto name = "_ZnwmRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, const std::nothrow_t &) noexcept>(
          &::operator new);

  static void *hook(size_t size, const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewAlignHook : HookBase {
  static constexpr auto name = "_ZnwmSt11align_val_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new);

  static void *hook(std::size_t size, std::align_val_t al) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZnwmSt11align_val_tRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t,
                            const std::nothrow_t &) noexcept>(&::operator new);

  static void *hook(std::size_t size, std::align_val_t al,
                    const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewArrayHook : HookBase {
  static constexpr auto name = "_Znam";
  static inline auto ref = static_cast<void *(*)(size_t)>(&::operator new[]);

  static void *hook(size_t size) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewArrayNoThrowHook : HookBase {
  static constexpr auto name = "_ZnamRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, const std::nothrow_t &) noexcept>(
          &::operator new[]);

  static void *hook(size_t size, const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewArrayAlignHook : HookBase {
  static constexpr auto name = "_ZnamSt11align_val_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new[]);

  static void *hook(std::size_t size, std::align_val_t al) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al);
    helper.track(ptr, size);
    return ptr;
  }
};

struct NewArrayAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZnamSt11align_val_tRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t,
                            const std::nothrow_t &) noexcept>(
          &::operator new[]);

  static void *hook(std::size_t size, std::align_val_t al,
                    const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct FreeHook : HookBase {
  static constexpr auto name = "free";
  static inline auto ref = &::free;

  static void hook(void *ptr) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }

    helper.track(ptr);
    ref(ptr);
  }
};

struct FreeSizedHook : HookBase {
  static constexpr auto name = "free_sized";
  static inline auto ref = &::free_sized;

  static void hook(void *ptr, size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }

    helper.track(ptr);
    ref(ptr, size);
  }
};

struct FreeAlignedSizedHook : HookBase {
  static constexpr auto name = "free_aligned_sized";
  static inline auto ref = &::free_aligned_sized;

  static void hook(void *ptr, size_t alignment, size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }

    helper.track(ptr);
    ref(ptr, alignment, size);
  }
};

struct DeleteHook : HookBase {
  static constexpr auto name = "_ZdlPv";
  static inline auto ref =
      static_cast<void (*)(void *) noexcept>(&::operator delete);

  static void hook(void *ptr) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr);
  }
};

struct DeleteArrayHook : HookBase {
  static constexpr auto name = "_ZdaPv";
  static inline auto ref =
      static_cast<void (*)(void *) noexcept>(&::operator delete[]);

  static void hook(void *ptr) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr);
  }
};

struct DeleteNoThrowHook : HookBase {
  static constexpr auto name = "_ZdlPvRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void (*)(void *, const std::nothrow_t &) noexcept>(
          &::operator delete);

  static void hook(void *ptr, const std::nothrow_t &tag) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, tag);
  }
};

struct DeleteArrayNoThrowHook : HookBase {
  static constexpr auto name = "_ZdaPvRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void (*)(void *, const std::nothrow_t &) noexcept>(
          &::operator delete[]);

  static void hook(void *ptr, const std::nothrow_t &tag) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, tag);
  }
};

struct DeleteAlignHook : HookBase {
  static constexpr auto name = "_ZdlPvSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::align_val_t) noexcept>(
          &::operator delete);

  static void hook(void *ptr, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, al);
  }
};

struct DeleteArrayAlignHook : HookBase {
  static constexpr auto name = "_ZdaPvSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::align_val_t) noexcept>(
          &::operator delete[]);

  static void hook(void *ptr, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, al);
  }
};

struct DeleteAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZdlPvSt11align_val_tRKSt9nothrow_t";
  static inline auto ref = static_cast<void (*)(
      void *, std::align_val_t, const std::nothrow_t &) noexcept>(
      &::operator delete);

  static void hook(void *ptr, std::align_val_t al,
                   const std::nothrow_t &tag) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, al, tag);
  }
};

struct DeleteArrayAlignNoThrowHook : HookBase {
  static constexpr auto name = "_ZdaPvSt11align_val_tRKSt9nothrow_t";
  static inline auto ref = static_cast<void (*)(
      void *, std::align_val_t, const std::nothrow_t &) noexcept>(
      &::operator delete[]);

  static void hook(void *ptr, std::align_val_t al,
                   const std::nothrow_t &tag) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, al, tag);
  }
};

struct DeleteSizedHook : HookBase {
  static constexpr auto name = "_ZdlPvm";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t) noexcept>(&::operator delete);

  static void hook(void *ptr, std::size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size);
  }
};

struct DeleteArraySizedHook : HookBase {
  static constexpr auto name = "_ZdaPvm";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t) noexcept>(&::operator delete[]);

  static void hook(void *ptr, std::size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size);
  }
};

struct DeleteSizedAlignHook : HookBase {
  static constexpr auto name = "_ZdlPvmSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t, std::align_val_t) noexcept>(
          &::operator delete);

  static void hook(void *ptr, std::size_t size, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size, al);
  }
};

struct DeleteArraySizedAlignHook : HookBase {
  static constexpr auto name = "_ZdaPvmSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t, std::align_val_t) noexcept>(
          &::operator delete[]);

  static void hook(void *ptr, std::size_t size, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size, al);
  }
};

struct CallocHook : HookBase {
  static constexpr auto name = "calloc";
  static inline auto ref = &::calloc;

  static void *hook(size_t nmemb, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(nmemb, size);
    helper.track(ptr, size * nmemb);
    return ptr;
  }
};

struct ReallocHook : HookBase {
  static constexpr auto name = "realloc";
  static inline auto ref = &::realloc;

  static void *hook(void *ptr, size_t size) noexcept {
    AllocTrackerHelper helper;
    if (likely(ptr) && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto *newptr = ref(ptr, size);
    if (likely(size)) {
      helper.track(newptr, size);
    }

    return newptr;
  }
};

struct PosixMemalignHook : HookBase {
  static constexpr auto name = "posix_memalign";
  static inline auto ref = &::posix_memalign;

  static int hook(void **memptr, size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto ret = ref(memptr, alignment, size);
    if (likely(!ret)) {
      helper.track(*memptr, size);
    }
    return ret;
  }
};

struct AlignedAllocHook : HookBase {
  static constexpr auto name = "aligned_alloc";
  static inline auto ref = &::aligned_alloc;

  static void *hook(size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(alignment, size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct MemalignHook : HookBase {
  static constexpr auto name = "memalign";
  static inline auto ref = &::memalign;

  static void *hook(size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(alignment, size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct PvallocHook : HookBase {
  static constexpr auto name = "pvalloc";
  static inline auto ref = &::pvalloc;

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct VallocHook : HookBase {
  static constexpr auto name = "valloc";
  static inline auto ref = &::valloc;

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct ReallocArrayHook : HookBase {
  static constexpr auto name = "reallocarray";
  static inline auto ref = &::reallocarray;

  static void *hook(void *ptr, size_t nmemb, size_t size) noexcept {
    AllocTrackerHelper helper;
    if (ptr && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto *newptr = ref(ptr, nmemb, size);
    if (newptr) {
      helper.track(newptr, size * nmemb);
    }
    return newptr;
  }
};

struct DlopenHook : HookBase {
  static constexpr auto name = "dlopen";
  static inline auto ref = &::dlopen;

  static void *hook(const char *filename, int flags) noexcept {
    void *ret = ref(filename, flags);
    update_overrides();
    return ret;
  }
};

struct MallocxHook : HookBase {
  static constexpr auto name = "mallocx";
  static inline auto ref = &::mallocx;

  static void *hook(size_t size, int flags) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, flags);
    helper.track(ptr, size);
    return ptr;
  }
};

struct RallocxHook : HookBase {
  static constexpr auto name = "rallocx";
  static inline auto ref = &::rallocx;

  static void *hook(void *ptr, size_t size, int flags) noexcept {
    AllocTrackerHelper helper;
    if (likely(ptr) && helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto *newptr = ref(ptr, size, flags);
    if (likely(size)) {
      helper.track(newptr, size);
    }

    return newptr;
  }
};

struct XallocxHook : HookBase {
  static constexpr auto name = "xallocx";
  static inline auto ref = &::xallocx;

  static size_t hook(void *ptr, size_t size, size_t extra, int flags) noexcept {
    AllocTrackerHelper helper;
    if (helper) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr), *helper.tl_state());
    }
    auto newsize = ref(ptr, size, extra, flags);
    helper.track(ptr, newsize);
    return newsize;
  }
};

struct DallocxHook : HookBase {
  static constexpr auto name = "dallocx";
  static inline auto ref = &::dallocx;

  static void hook(void *ptr, int flags) noexcept {
    DeallocTrackerHelper helper;
    ref(ptr, flags);
    helper.track(ptr);
  }
};

struct SdallocxHook : HookBase {
  static constexpr auto name = "sdallocx";
  static inline auto ref = &::sdallocx;

  static void hook(void *ptr, size_t size, int flags) noexcept {
    DeallocTrackerHelper helper;
    ref(ptr, size, flags);
    helper.track(ptr);
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
  static inline auto ref = &::pthread_create;

  static int hook(pthread_t *thread, const pthread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg) noexcept {
    Args *args = new (std::nothrow) Args{start_routine, arg};
    return args ? ref(thread, attr, &my_start, args)
                : ref(thread, attr, start_routine, arg);
  }
};

struct MmapHook : HookBase {
  static constexpr auto name = "mmap";
  static inline auto ref = &::mmap;

  static void *hook(void *addr, size_t length, int prot, int flags, int fd,
                    off_t offset) noexcept {
    AllocTrackerHelper helper;
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr) {
      helper.track(ptr, length);
    }
    return ptr;
  }
};

struct Mmap_Hook : HookBase {
  static constexpr auto name = "__mmap";
  static inline auto ref = &::mmap;

  static void *hook(void *addr, size_t length, int prot, int flags, int fd,
                    off_t offset) noexcept {
    AllocTrackerHelper helper;
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr) {
      helper.track(ptr, length);
    }
    return ptr;
  }
};

struct Mmap64Hook : HookBase {
  static constexpr auto name = "mmap64";
  static inline auto ref = &::mmap64;

  static void *hook(void *addr, size_t length, int prot, int flags, int fd,
                    off_t offset) noexcept {
    AllocTrackerHelper helper;
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr) {
      helper.track(ptr, length);
    }
    return ptr;
  }
};

struct MunmapHook : HookBase {
  static constexpr auto name = "munmap";
  static inline auto ref = &::munmap;

  static int hook(void *addr, size_t length) noexcept {
    DeallocTrackerHelper helper;
    helper.track(addr);
    return ref(addr, length);
  }
};

struct Munmap_Hook : HookBase {
  static constexpr auto name = "__munmap";
  static inline auto ref = &::munmap;

  static int hook(void *addr, size_t length) noexcept {
    DeallocTrackerHelper helper;
    helper.track(addr);
    return ref(addr, length);
  }
};

template <typename T> void register_hook() {
  // Be careful not to override T::ref (compiler/linker may emit T::ref as a
  // relocation pointing on malloc/realloc/calloc/...)
  g_symbol_overrides->register_override(T::name,
                                        reinterpret_cast<uintptr_t>(&T::hook),
                                        reinterpret_cast<uintptr_t *>(&T::ref),
                                        reinterpret_cast<uintptr_t>(&T::ref));
}

void register_hooks() {
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

  if (ReallocArrayHook::ref) {
    register_hook<ReallocArrayHook>();
  }
  if (PvallocHook::ref) {
    register_hook<PvallocHook>();
  }

  register_hook<PthreadCreateHook>();
  register_hook<DlopenHook>();
}

enum class AllocFunctionId { kMalloc = 1, kRealloc, kCalloc };

} // namespace

void setup_overrides(OverrideMode mode) {
  std::lock_guard const lock(g_mutex);
  MaybeReentryGuard const guard; // avoid tracking allocations

  if (mode == OverrideMode::kGOTOverride) {
    if (!g_symbol_overrides) {
      g_symbol_overrides = std::make_unique<SymbolOverrides>();
      register_hooks();
    }

    g_symbol_overrides->apply_overrides();
  } else {
    // standard allocation functions
    ddprof::instrument_function("calloc",
                                static_cast<int>(AllocFunctionId::kCalloc));
    ddprof::instrument_function("realloc",
                                static_cast<int>(AllocFunctionId::kRealloc));
    ddprof::instrument_function("malloc",
                                static_cast<int>(AllocFunctionId::kMalloc));
    // jemalloc in Rust
    ddprof::instrument_function("_rjem_mallocx",
                                static_cast<int>(AllocFunctionId::kMalloc));
    ddprof::instrument_function("_rjem_rallocx",
                                static_cast<int>(AllocFunctionId::kRealloc));
    ddprof::instrument_function("_rjem_malloc",
                                static_cast<int>(AllocFunctionId::kMalloc));
    ddprof::instrument_function("_rjem_calloc",
                                static_cast<int>(AllocFunctionId::kCalloc));

    // tcmalloc provides its own version new
    ddprof::instrument_function("operator new(unsigned long)",
                                static_cast<int>(AllocFunctionId::kMalloc));
    ddprof::instrument_function("operator new[](unsigned long)",
                                static_cast<int>(AllocFunctionId::kMalloc));

    /* \fixme{nsavoire} we should probably also instrument:
       - other standard allocation functions: free, posix_memalign,
         aligned_alloc, ...
       - jemmalloc non-standard functions: mallocx, xallocx, rallocx, sallocx,
         nallocx, sdallocx, ...
       - other new operators for tcmalloc (and operator delete)
       - nallocx/sdallocx for tcmalloc

       Ideally instrumented function should not overlap each other, but it might
       not always be possible (new operator from libstdc++ will call malloc).
       */
  }
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

void EntryPayload(uint64_t return_address, uint64_t function_id,
                  uint64_t stack_pointer, uint64_t return_trampoline_address) {

  uint64_t *stackp = reinterpret_cast<uint64_t *>(stack_pointer);
  ddprof::AllocFunctionId alloc_func_id =
      static_cast<ddprof::AllocFunctionId>(function_id);
  uint64_t arg1 = stackp[-5];
  uint64_t arg2 = stackp[-4];
  uint64_t size = 0;

  switch (alloc_func_id) {
  case ddprof::AllocFunctionId::kMalloc:
    size = arg1;
    break;
  case ddprof::AllocFunctionId::kCalloc:
    size = arg1 * arg2;
    break;
  case ddprof::AllocFunctionId::kRealloc:
    size = arg2;
    break;
  }
  auto *tl_state = ddprof::AllocationTracker::get_tl_state();

  if (tl_state) {
    ddprof::ReentryGuard guard{&tl_state->reentry_guard};
    if (guard) {
      ddprof::AllocationTracker::track_allocation_s(0, size, *tl_state, stackp);
    }
  }
}

uint64_t ExitPayload() {
  printf("ExitPayload\n");
  return 0;
}
