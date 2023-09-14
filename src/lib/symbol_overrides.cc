// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbol_overrides.hpp"

#include "allocation_tracker.hpp"
#include "ddprof_base.hpp"
#include "elfutils.hpp"
#include "reentry_guard.hpp"
#include "unlikely.hpp"

#include <cstdlib>
#include <dlfcn.h>
#include <malloc.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>

#if defined(__GNUC__) && !defined(__clang__)
#  define NOEXCEPT noexcept
#else
#  define NOEXCEPT
#endif

extern "C" {
// Declaration of reallocarray is only available starting from glibc 2.28
__attribute__((weak)) void *reallocarray(void *ptr, size_t nmemb,
                                         size_t size) NOEXCEPT;
__attribute__((weak)) void *pvalloc(size_t size) NOEXCEPT;
// NOLINTNEXTLINE cert-dcl51-cpp
__attribute__((weak)) int __libc_allocate_rtsig(int high) NOEXCEPT;
}

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
  int nb = ddprof::count_loaded_libraries();
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

struct malloc {
  static constexpr auto name = "malloc";
  static inline auto ref = &::malloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ptr = ref(size);
    if (guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *tl_state);
    }
    return ptr;
  }
};

struct free {
  static constexpr auto name = "free";
  static inline auto ref = &::free;
  static inline bool ref_checked = false;

  static void hook(void *ptr) noexcept {
    check_libraries();
    if (ptr == nullptr) {
      return;
    }

    ddprof::AllocationTracker::track_deallocation_s(
        reinterpret_cast<uintptr_t>(ptr));
    ref(ptr);
  }
};

struct calloc {
  static constexpr auto name = "calloc";
  static inline auto ref = &::calloc;
  static inline bool ref_checked = false;

  static void *hook(size_t nmemb, size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ptr = ref(nmemb, size);
    if (guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size * nmemb, *tl_state);
    }
    return ptr;
  }
};

struct realloc {
  static constexpr auto name = "realloc";
  static inline auto ref = &::realloc;
  static inline bool ref_checked = false;

  static void *hook(void *ptr, size_t size) noexcept {
    check_libraries();
    if (likely(ptr)) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr));
    }
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    // lifetime of guard should exceed the call to ref function
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto newptr = ref(ptr, size);
    if (likely(size) && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(newptr), size, *tl_state);
    }

    return newptr;
  }
};

struct posix_memalign {
  static constexpr auto name = "posix_memalign";
  static inline auto ref = &::posix_memalign;
  static inline bool ref_checked = false;

  static int hook(void **memptr, size_t alignment, size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ret = ref(memptr, alignment, size);
    if (likely(!ret) && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(*memptr), size, *tl_state);
    }
    return ret;
  }
};

struct aligned_alloc {
  static constexpr auto name = "aligned_alloc";
  static inline auto ref = &::aligned_alloc;
  static inline bool ref_checked = false;

  static void *hook(size_t alignment, size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ptr = ref(alignment, size);
    if (ptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *tl_state);
    }
    return ptr;
  }
};

struct memalign {
  static constexpr auto name = "memalign";
  static inline auto ref = &::memalign;
  static inline bool ref_checked = false;

  static void *hook(size_t alignment, size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ptr = ref(alignment, size);
    if (ptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *tl_state);
    }
    return ptr;
  }
};

struct pvalloc {
  static constexpr auto name = "pvalloc";
  static inline auto ref = &::pvalloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ptr = ref(size);
    if (ptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *tl_state);
    }
    return ptr;
  }
};

struct valloc {
  static constexpr auto name = "valloc";
  static inline auto ref = &::valloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    check_libraries();
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto ptr = ref(size);
    if (ptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), size, *tl_state);
    }
    return ptr;
  }
};

struct reallocarray {
  static constexpr auto name = "reallocarray";
  static inline auto ref = &::reallocarray;
  static inline bool ref_checked = false;

  static void *hook(void *ptr, size_t nmemb, size_t size) noexcept {
    check_libraries();
    if (ptr) {
      ddprof::AllocationTracker::track_deallocation_s(
          reinterpret_cast<uintptr_t>(ptr));
    }
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    auto newptr = ref(ptr, nmemb, size);
    if (newptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(newptr), size * nmemb, *tl_state);
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
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), length, *tl_state);
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
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), length, *tl_state);
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
    ddprof::TrackerThreadLocalState *tl_state =
        ddprof::AllocationTracker::get_tl_state();
    ddprof::ReentryGuard guard(tl_state ? &(tl_state->double_tracking_guard)
                                        : nullptr);
    void *ptr = ref(addr, length, prot, flags, fd, offset);
    if (addr == nullptr && fd == -1 && ptr != nullptr && guard) {
      ddprof::AllocationTracker::track_allocation_s(
          reinterpret_cast<uintptr_t>(ptr), length, *tl_state);
    }
    return ptr;
  }
};

struct munmap {
  static constexpr auto name = "munmap";
  static inline auto ref = &::munmap;
  static inline bool ref_checked = false;

  static int hook(void *addr, size_t length) noexcept {
    ddprof::AllocationTracker::track_deallocation_s(
        reinterpret_cast<uintptr_t>(addr));
    return ref(addr, length);
  }
};

struct munmap_ {
  static constexpr auto name = "__munmap";
  static inline auto ref = &::munmap;
  static inline bool ref_checked = false;

  static int hook(void *addr, size_t length) noexcept {
    ddprof::AllocationTracker::track_deallocation_s(
        reinterpret_cast<uintptr_t>(addr));
    return ref(addr, length);
  }
};

template <typename T> void install_hook(bool restore) {
  // On ubuntu 16, some symbols might be bound to <symbol>@plt symbols
  // in exe and since we override the symbols in the exe, this would cause
  // infinite recursion. To workaround this, we do an explicit lookup (we don't
  // use dlsym since it would return the same <symbol>>@plt symbol).
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

void timer_handler(int, siginfo_t *, void *) {
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

constexpr timespec duration_to_timespec(std::chrono::milliseconds d) {
  auto nsecs = std::chrono::duration_cast<std::chrono::seconds>(d);
  d -= nsecs;

  return timespec{nsecs.count(), std::chrono::nanoseconds(d).count()};
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

  itimerspec its = {
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
  install_hook<calloc>(restore);
  install_hook<realloc>(restore);
  install_hook<posix_memalign>(restore);
  install_hook<aligned_alloc>(restore);
  install_hook<memalign>(restore);
  install_hook<valloc>(restore);

  install_hook<mmap>(restore);
  install_hook<mmap64_>(restore);
  install_hook<munmap>(restore);
  install_hook<mmap_>(restore);
  install_hook<munmap_>(restore);

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

namespace ddprof {
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
