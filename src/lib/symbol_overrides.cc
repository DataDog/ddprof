// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbol_overrides.hpp"

#include "allocation_tracker.hpp"
#include "chrono_utils.hpp"
#include "ddprof_base.hpp"
#include "elfutils.hpp"
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
__attribute__((weak)) void *reallocarray(void *ptr, size_t nmemb,
                                         size_t nmenb) NOEXCEPT;
__attribute__((weak)) void *pvalloc(size_t size) NOEXCEPT;
__attribute__((weak)) int __libc_allocate_rtsig(int high) NOEXCEPT;
// NOLINTEND

// sized free functions (C23, not yet available in glibc)
__attribute__((weak)) void free_sized(void *ptr, size_t size);
__attribute__((weak)) void free_aligned_sized(void *ptr, size_t alignment,
                                              size_t size);

// jemalloc Non-standard API
__attribute__((weak)) void *mallocx(size_t size, int flags);
__attribute__((weak)) void *rallocx(void *ptr, size_t size, int flags);
__attribute__((weak)) size_t xallocx(void *ptr, size_t size, size_t extra,
                                     int flags);
__attribute__((weak)) void dallocx(void *ptr, int flags);
__attribute__((weak)) void sdallocx(void *ptr, size_t size, int flags);
}

namespace ddprof {
namespace {
constexpr int k_sigrtmax_offset = 3;

std::chrono::milliseconds g_initial_loaded_libs_check_delay;
std::chrono::milliseconds g_loaded_libs_check_interval;
bool g_symbols_overridden = false;
bool g_check_libraries = false;
bool g_timer_active = false;
timer_t g_timerid;
int g_timer_sig = -1;
int g_nb_loaded_libraries = -1;

void setup_hooks(bool restore);

DDPROF_NOINLINE bool loaded_libraries_have_changed() {
  int const nb = ddprof::count_loaded_libraries();
  if (nb != g_nb_loaded_libraries) {
    g_nb_loaded_libraries = nb;
    return true;
  }
  return false;
}

void check_libraries() {
  // \fixme{nsavoire} Race condition here, convert g_check_libraries to an
  // atomic ?
  if (g_check_libraries) {
    if (g_symbols_overridden && loaded_libraries_have_changed()) {
      setup_hooks(false);
    }
    g_check_libraries = false;
  }
}

class AllocTrackerHelper {
public:
  AllocTrackerHelper()
      : _tl_state{ddprof::AllocationTracker::get_tl_state()},
        _guard{_tl_state ? &(_tl_state->reentry_guard) : nullptr} {
    check_libraries();
  }

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

struct malloc {
  static constexpr auto name = "malloc";
  static inline auto ref = &::malloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_ {
  static constexpr auto name = "_Znwm";
  static inline auto ref = static_cast<void *(*)(size_t)>(&::operator new);
  static inline bool ref_checked = false;

  static void *hook(size_t size) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_nothrow {
  static constexpr auto name = "_ZnwmRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, const std::nothrow_t &) noexcept>(
          &::operator new);
  static inline bool ref_checked = false;

  static void *hook(size_t size, const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_align {
  static constexpr auto name = "_ZnwmSt11align_val_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new);
  static inline bool ref_checked = false;

  static void *hook(std::size_t size, std::align_val_t al) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_align_nothrow {
  static constexpr auto name = "_ZnwmSt11align_val_tRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t,
                            const std::nothrow_t &) noexcept>(&::operator new);
  static inline bool ref_checked = false;

  static void *hook(std::size_t size, std::align_val_t al,
                    const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_array {
  static constexpr auto name = "_Znam";
  static inline auto ref = static_cast<void *(*)(size_t)>(&::operator new[]);
  static inline bool ref_checked = false;

  static void *hook(size_t size) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_array_nothrow {
  static constexpr auto name = "_ZnamRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, const std::nothrow_t &) noexcept>(
          &::operator new[]);
  static inline bool ref_checked = false;

  static void *hook(size_t size, const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_array_align {
  static constexpr auto name = "_ZnamSt11align_val_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new[]);
  static inline bool ref_checked = false;

  static void *hook(std::size_t size, std::align_val_t al) {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al);
    helper.track(ptr, size);
    return ptr;
  }
};

struct new_array_align_nothrow {
  static constexpr auto name = "_ZnamSt11align_val_tRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void *(*)(size_t, std::align_val_t,
                            const std::nothrow_t &) noexcept>(
          &::operator new[]);
  static inline bool ref_checked = false;

  static void *hook(std::size_t size, std::align_val_t al,
                    const std::nothrow_t &tag) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, al, tag);
    helper.track(ptr, size);
    return ptr;
  }
};

struct free {
  static constexpr auto name = "free";
  static inline auto ref = &::free;
  static inline bool ref_checked = false;

  static void hook(void *ptr) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }

    helper.track(ptr);
    ref(ptr);
  }
};

struct free_sized {
  static constexpr auto name = "free_sized";
  static inline auto ref = &::free_sized;
  static inline bool ref_checked = false;

  static void hook(void *ptr, size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }

    helper.track(ptr);
    ref(ptr, size);
  }
};

struct free_aligned_sized {
  static constexpr auto name = "free_aligned_sized";
  static inline auto ref = &::free_aligned_sized;
  static inline bool ref_checked = false;

  static void hook(void *ptr, size_t alignment, size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }

    helper.track(ptr);
    ref(ptr, alignment, size);
  }
};

struct delete_ {
  static constexpr auto name = "_ZdlPv";
  static inline auto ref =
      static_cast<void (*)(void *) noexcept>(&::operator delete);
  static inline bool ref_checked = false;

  static void hook(void *ptr) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr);
  }
};

struct delete_array {
  static constexpr auto name = "_ZdaPv";
  static inline auto ref =
      static_cast<void (*)(void *) noexcept>(&::operator delete[]);
  static inline bool ref_checked = false;

  static void hook(void *ptr) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr);
  }
};

struct delete_nothrow {
  static constexpr auto name = "_ZdlPvRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void (*)(void *, const std::nothrow_t &) noexcept>(
          &::operator delete);
  static inline bool ref_checked = false;

  static void hook(void *ptr, const std::nothrow_t &tag) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, tag);
  }
};

struct delete_array_nothrow {
  static constexpr auto name = "_ZdaPvRKSt9nothrow_t";
  static inline auto ref =
      static_cast<void (*)(void *, const std::nothrow_t &) noexcept>(
          &::operator delete[]);
  static inline bool ref_checked = false;

  static void hook(void *ptr, const std::nothrow_t &tag) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, tag);
  }
};

struct delete_align {
  static constexpr auto name = "_ZdlPvSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::align_val_t) noexcept>(
          &::operator delete);
  static inline bool ref_checked = false;

  static void hook(void *ptr, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, al);
  }
};

struct delete_array_align {
  static constexpr auto name = "_ZdaPvSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::align_val_t) noexcept>(
          &::operator delete[]);
  static inline bool ref_checked = false;

  static void hook(void *ptr, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, al);
  }
};

struct delete_align_nothrow {
  static constexpr auto name = "_ZdlPvSt11align_val_tRKSt9nothrow_t";
  static inline auto ref = static_cast<void (*)(
      void *, std::align_val_t, const std::nothrow_t &) noexcept>(
      &::operator delete);
  static inline bool ref_checked = false;

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

struct delete_array_align_nothrow {
  static constexpr auto name = "_ZdaPvSt11align_val_tRKSt9nothrow_t";
  static inline auto ref = static_cast<void (*)(
      void *, std::align_val_t, const std::nothrow_t &) noexcept>(
      &::operator delete[]);
  static inline bool ref_checked = false;

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

struct delete_sized {
  static constexpr auto name = "_ZdlPvm";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t) noexcept>(&::operator delete);
  static inline bool ref_checked = false;

  static void hook(void *ptr, std::size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size);
  }
};

struct delete_array_sized {
  static constexpr auto name = "_ZdaPvm";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t) noexcept>(&::operator delete[]);
  static inline bool ref_checked = false;

  static void hook(void *ptr, std::size_t size) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size);
  }
};

struct delete_sized_align {
  static constexpr auto name = "_ZdlPvmSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t, std::align_val_t) noexcept>(
          &::operator delete);
  static inline bool ref_checked = false;

  static void hook(void *ptr, std::size_t size, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size, al);
  }
};

struct delete_array_sized_align {
  static constexpr auto name = "_ZdaPvmSt11align_val_t";
  static inline auto ref =
      static_cast<void (*)(void *, std::size_t, std::align_val_t) noexcept>(
          &::operator delete[]);
  static inline bool ref_checked = false;

  static void hook(void *ptr, std::size_t size, std::align_val_t al) noexcept {
    DeallocTrackerHelper helper;
    if (ptr == nullptr) {
      return;
    }
    helper.track(ptr);
    ref(ptr, size, al);
  }
};

struct calloc {
  static constexpr auto name = "calloc";
  static inline auto ref = &::calloc;
  static inline bool ref_checked = false;

  static void *hook(size_t nmemb, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(nmemb, size);
    helper.track(ptr, size * nmemb);
    return ptr;
  }
};

struct realloc {
  static constexpr auto name = "realloc";
  static inline auto ref = &::realloc;
  static inline bool ref_checked = false;

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

struct posix_memalign {
  static constexpr auto name = "posix_memalign";
  static inline auto ref = &::posix_memalign;
  static inline bool ref_checked = false;

  static int hook(void **memptr, size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto ret = ref(memptr, alignment, size);
    if (likely(!ret)) {
      helper.track(*memptr, size);
    }
    return ret;
  }
};

struct aligned_alloc {
  static constexpr auto name = "aligned_alloc";
  static inline auto ref = &::aligned_alloc;
  static inline bool ref_checked = false;

  static void *hook(size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(alignment, size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct memalign {
  static constexpr auto name = "memalign";
  static inline auto ref = &::memalign;
  static inline bool ref_checked = false;

  static void *hook(size_t alignment, size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(alignment, size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct pvalloc {
  static constexpr auto name = "pvalloc";
  static inline auto ref = &::pvalloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct valloc {
  static constexpr auto name = "valloc";
  static inline auto ref = &::valloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size);
    if (ptr) {
      helper.track(ptr, size);
    }
    return ptr;
  }
};

struct reallocarray {
  static constexpr auto name = "reallocarray";
  static inline auto ref = &::reallocarray;
  static inline bool ref_checked = false;

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

struct dlopen {
  static constexpr auto name = "dlopen";
  static inline auto ref = &::dlopen;
  static inline bool ref_checked = false;

  static void *hook(const char *filename, int flags) noexcept {
    void *ret = ref(filename, flags);
    if (g_symbols_overridden) {
      setup_hooks(false);
    }
    return ret;
  }
};

struct mallocx {
  static constexpr auto name = "mallocx";
  static inline auto ref = &::mallocx;
  static inline bool ref_checked = false;

  static void *hook(size_t size, int flags) noexcept {
    AllocTrackerHelper helper;
    auto *ptr = ref(size, flags);
    helper.track(ptr, size);
    return ptr;
  }
};

struct rallocx {
  static constexpr auto name = "rallocx";
  static inline auto ref = &::rallocx;
  static inline bool ref_checked = false;

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

struct xallocx {
  static constexpr auto name = "xallocx";
  static inline auto ref = &::xallocx;
  static inline bool ref_checked = false;

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

struct dallocx {
  static constexpr auto name = "dallocx";
  static inline auto ref = &::dallocx;
  static inline bool ref_checked = false;

  static void hook(void *ptr, int flags) noexcept {
    DeallocTrackerHelper helper;
    ref(ptr, flags);
    helper.track(ptr);
  }
};

struct sdallocx {
  static constexpr auto name = "sdallocx";
  static inline auto ref = &::sdallocx;
  static inline bool ref_checked = false;

  static void hook(void *ptr, size_t size, int flags) noexcept {
    DeallocTrackerHelper helper;
    ref(ptr, size, flags);
    helper.track(ptr);
  }
};

using Args = std::tuple<void *(*)(void *), void *>;

void *mystart(void *arg) {
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
 * - AllocationTracker tracks the allocation and calls savecontext
 * - savecontext calls pthread_getattr to get stack end address
 * - pthread_getattr is reentered and attempts to take the lock again leading to
 * a deadlock.
 *
 * Workaround is to hook pthread_create and call `cache_stack_end` to
 * cache stack end address while temporarily disabling allocation profiling for
 * current thread before calling user code.
 * */
struct pthread_create {
  static constexpr auto name = "pthread_create";
  static inline auto ref = &::pthread_create;
  static inline bool ref_checked = false;

  static int hook(pthread_t *thread, const pthread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg) noexcept {
    Args *args = new (std::nothrow) Args{start_routine, arg};
    return args ? ref(thread, attr, &mystart, args)
                : ref(thread, attr, start_routine, arg);
  }
};

struct mmap {
  static constexpr auto name = "mmap";
  static inline auto ref = &::mmap;
  static inline bool ref_checked = false;

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

struct mmap_ {
  static constexpr auto name = "__mmap";
  static inline auto ref = &::mmap;
  static inline bool ref_checked = false;

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

struct mmap64_ {
  static constexpr auto name = "mmap64";
  static inline auto ref = &::mmap64;
  static inline bool ref_checked = false;

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

struct munmap {
  static constexpr auto name = "munmap";
  static inline auto ref = &::munmap;
  static inline bool ref_checked = false;

  static int hook(void *addr, size_t length) noexcept {
    DeallocTrackerHelper helper;
    helper.track(addr);
    return ref(addr, length);
  }
};

struct munmap_ {
  static constexpr auto name = "__munmap";
  static inline auto ref = &::munmap;
  static inline bool ref_checked = false;

  static int hook(void *addr, size_t length) noexcept {
    DeallocTrackerHelper helper;
    helper.track(addr);
    return ref(addr, length);
  }
};

template <typename T> void install_hook(bool restore) {
  // On ubuntu 16, some symbols might be bound to <symbol>@plt symbols
  // in exe and since we override the symbols in the exe, this would cause
  // infinite recursion. To workaround this, we do an explicit lookup (we don't
  // use dlsym since it would return the same <symbol>@plt symbol).
  if (!restore && !T::ref_checked) {
    ElfW(Sym) sym = ddprof::lookup_symbol(T::name, true);
    if (sym.st_size == 0 &&
        reinterpret_cast<decltype(T::ref)>(sym.st_value) == T::ref) {
      // null sized symbol, look for a non-null sized symbol
      sym = ddprof::lookup_symbol(T::name, false);
      if (sym.st_value && sym.st_size) {
        T::ref = reinterpret_cast<decltype(T::ref)>(sym.st_value);
      }
    }
    T::ref_checked = true;
  }
  // Be careful not to override T::ref (compiler/linker may emit T::ref as a
  // relocation pointing on malloc/realloc/calloc/...)
  ddprof::override_symbol(
      T::name, reinterpret_cast<void *>(restore ? T::ref : &T::hook), &T::ref);
}

void timer_handler(int /*sig*/, siginfo_t * /*info*/, void * /*ucontext*/) {
  // check libraries only if symbols are overriden
  g_check_libraries = g_symbols_overridden;
}

void uninstall_handler() {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = SIG_IGN;
  sigaction(g_timer_sig, &sa, NULL);
}

void uninstall_timer() {
  uninstall_handler();
  timer_delete(g_timerid);
  g_timer_active = false;
}

int install_timer(std::chrono::milliseconds initial_loaded_libs_check_delay,
                  std::chrono::milliseconds loaded_libs_check_interval) {
  if (g_timer_active ||
      (initial_loaded_libs_check_delay.count() == 0 &&
       loaded_libs_check_interval.count() == 0)) {
    return 0;
  }

  if (g_timer_sig == -1) {
    if (__libc_allocate_rtsig) {
      // If available, use private libc function to allocate a free signal
      g_timer_sig = __libc_allocate_rtsig(1);
    } else {
      // Pick an arbitrary signal
      g_timer_sig = std::max(SIGRTMIN, SIGRTMAX - k_sigrtmax_offset);
    }
  }
  if (g_timer_sig == -1) {
    return -1;
  }

  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sa.sa_sigaction = timer_handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(g_timer_sig, &sa, NULL) == -1) {
    return -1;
  }

  sigevent sev;
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = g_timer_sig;
  sev.sigev_value.sival_ptr = &g_timerid;
  if (timer_create(CLOCK_MONOTONIC, &sev, &g_timerid) == -1) {
    uninstall_handler();
    return -1;
  }

  itimerspec const its = {
      .it_interval = duration_to_timespec(loaded_libs_check_interval),
      .it_value = duration_to_timespec(initial_loaded_libs_check_delay)};
  if (timer_settime(g_timerid, 0, &its, NULL) == -1) {
    uninstall_timer();
    return -1;
  }

  g_timer_active = true;
  g_initial_loaded_libs_check_delay = initial_loaded_libs_check_delay;
  g_loaded_libs_check_interval = loaded_libs_check_interval;
  return 0;
}

void setup_hooks(bool restore) {
  install_hook<malloc>(restore);
  install_hook<free>(restore);
  install_hook<free_sized>(restore);
  install_hook<free_aligned_sized>(restore);
  install_hook<calloc>(restore);
  install_hook<realloc>(restore);
  install_hook<posix_memalign>(restore);
  install_hook<aligned_alloc>(restore);
  install_hook<memalign>(restore);
  install_hook<valloc>(restore);

  install_hook<new_>(restore);
  install_hook<new_array>(restore);
  install_hook<new_nothrow>(restore);
  install_hook<new_array_nothrow>(restore);
  install_hook<new_align>(restore);
  install_hook<new_array_align>(restore);
  install_hook<new_align_nothrow>(restore);
  install_hook<new_array_align_nothrow>(restore);

  install_hook<delete_>(restore);
  install_hook<delete_array>(restore);
  install_hook<delete_nothrow>(restore);
  install_hook<delete_array_nothrow>(restore);
  install_hook<delete_align>(restore);
  install_hook<delete_array_align>(restore);
  install_hook<delete_align_nothrow>(restore);
  install_hook<delete_array_align_nothrow>(restore);
  install_hook<delete_sized>(restore);
  install_hook<delete_array_sized>(restore);
  install_hook<delete_sized_align>(restore);
  install_hook<delete_array_sized_align>(restore);

  install_hook<mmap>(restore);
  install_hook<mmap64_>(restore);
  install_hook<munmap>(restore);
  install_hook<mmap_>(restore);
  install_hook<munmap_>(restore);

  install_hook<mallocx>(restore);
  install_hook<rallocx>(restore);
  install_hook<xallocx>(restore);
  install_hook<dallocx>(restore);
  install_hook<sdallocx>(restore);

  if (reallocarray::ref) {
    install_hook<reallocarray>(restore);
  }
  if (pvalloc::ref) {
    install_hook<pvalloc>(restore);
  }

  install_hook<pthread_create>(restore);
  install_hook<dlopen>(restore);

  g_symbols_overridden = !restore;
}

} // namespace

void setup_overrides(std::chrono::milliseconds initial_loaded_libs_check_delay,
                     std::chrono::milliseconds loaded_libs_check_interval) {
  setup_hooks(false);
  install_timer(initial_loaded_libs_check_delay, loaded_libs_check_interval);
}

void restore_overrides() {
  setup_hooks(true);
  if (g_timer_active) {
    uninstall_timer();
  }
}

void reinstall_timer_after_fork() {
  g_timer_active = false;
  if (g_symbols_overridden) {
    install_timer(g_initial_loaded_libs_check_delay,
                  g_loaded_libs_check_interval);
  }
}
} // namespace ddprof
