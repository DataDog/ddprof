// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbol_overrides.hpp"

#include "allocation_tracker.hpp"
#include "ddprof_base.hpp"
#include "elfutils.hpp"
#include "unlikely.hpp"

#include <cstdlib>
#include <dlfcn.h>
#include <malloc.h>
#include <sys/mman.h>

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
}

namespace {

bool g_symbols_overridden = false;
// \fixme{nsavoire} We should probably merge this TL state with
// AllocationTracker::tl_state to have a single TL state since accessing it is
// costly (call to tls_get_addr)
thread_local bool g_in_allocator_guard = false;

class Guard {
public:
  explicit Guard(bool *guard) : _guard(guard), _ok(!*guard) { *_guard = true; }
  ~Guard() {
    if (_ok) {
      *_guard = false;
    }
  }
  explicit operator bool() const { return _ok; }

private:
  bool *_guard;
  bool _ok;
};

struct malloc {
  static constexpr auto name = "malloc";
  static inline auto ref = &::malloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);

    auto ptr = ref(size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(ptr), size);

    return ptr;
  }
};

struct free {
  static constexpr auto name = "free";
  static inline auto ref = &::free;
  static inline bool ref_checked = false;

  static void hook(void *ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }

    ddprof::AllocationTracker::track_deallocation(
        reinterpret_cast<uintptr_t>(ptr));
    ref(ptr);
  }
};

struct calloc {
  static constexpr auto name = "calloc";
  static inline auto ref = &::calloc;
  static inline bool ref_checked = false;

  static void *hook(size_t nmemb, size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    auto ptr = ref(nmemb, size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(ptr), size * nmemb);

    return ptr;
  }
};

struct realloc {
  static constexpr auto name = "realloc";
  static inline auto ref = &::realloc;
  static inline bool ref_checked = false;

  static void *hook(void *ptr, size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    if (likely(ptr)) {
      ddprof::AllocationTracker::track_deallocation(
          reinterpret_cast<uintptr_t>(ptr));
    }

    auto newptr = ref(ptr, size);
    if (likely(size)) {
      ddprof::AllocationTracker::track_allocation(
          reinterpret_cast<uintptr_t>(newptr), size);
    }

    return newptr;
  }
};

struct posix_memalign {
  static constexpr auto name = "posix_memalign";
  static inline auto ref = &::posix_memalign;
  static inline bool ref_checked = false;

  static int hook(void **memptr, size_t alignment, size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    auto ret = ref(memptr, alignment, size);
    if (likely(!ret)) {
      ddprof::AllocationTracker::track_allocation(
          reinterpret_cast<uintptr_t>(*memptr), size);
    }
    return ret;
  }
};

struct aligned_alloc {
  static constexpr auto name = "aligned_alloc";
  static inline auto ref = &::aligned_alloc;
  static inline bool ref_checked = false;

  static void *hook(size_t alignment, size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    auto ptr = ref(alignment, size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(ptr), size);

    return ptr;
  }
};

struct memalign {
  static constexpr auto name = "memalign";
  static inline auto ref = &::memalign;
  static inline bool ref_checked = false;

  static void *hook(size_t alignment, size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    auto ptr = ref(alignment, size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(ptr), size);

    return ptr;
  }
};

struct pvalloc {
  static constexpr auto name = "pvalloc";
  static inline auto ref = &::pvalloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    auto ptr = ref(size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(ptr), size);

    return ptr;
  }
};

struct valloc {
  static constexpr auto name = "valloc";
  static inline auto ref = &::valloc;
  static inline bool ref_checked = false;

  static void *hook(size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    auto ptr = ref(size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(ptr), size);

    return ptr;
  }
};

struct reallocarray {
  static constexpr auto name = "reallocarray";
  static inline auto ref = &::reallocarray;
  static inline bool ref_checked = false;

  static void *hook(void *ptr, size_t nmemb, size_t size) noexcept {
    Guard guard(&g_in_allocator_guard);
    if (ptr) {
      ddprof::AllocationTracker::track_deallocation(
          reinterpret_cast<uintptr_t>(ptr));
    }
    auto newptr = ref(ptr, nmemb, size);
    ddprof::AllocationTracker::track_allocation(
        reinterpret_cast<uintptr_t>(newptr), size * nmemb);
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
      ddprof::setup_overrides();
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
    void *ptr = ref(addr, length, prot, flags, fd, offset);

    if (addr == nullptr && fd == -1 && ptr != nullptr &&
        !g_in_allocator_guard) {
      ddprof::AllocationTracker::track_allocation(
          reinterpret_cast<uintptr_t>(ptr), length);
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
    void *ptr = ref(addr, length, prot, flags, fd, offset);

    if (addr == nullptr && fd == -1 && ptr != nullptr &&
        !g_in_allocator_guard) {
      ddprof::AllocationTracker::track_allocation(
          reinterpret_cast<uintptr_t>(ptr), length);
    }
    return ptr;
  }
};

struct munmap {
  static constexpr auto name = "munmap";
  static inline auto ref = &::munmap;
  static inline bool ref_checked = false;

  static int hook(void *addr, size_t length) noexcept {
    if (!g_in_allocator_guard) {
      ddprof::AllocationTracker::track_deallocation(
          reinterpret_cast<uintptr_t>(addr));
    }
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
    if (sym.st_size == 0) {
      // null sized symbol, look for a non-null sized symbol
      sym = ddprof::lookup_symbol(T::name, false);
      if (sym.st_value && sym.st_size) {
        T::ref = reinterpret_cast<decltype(T::ref)>(sym.st_value);
      }
    }
    T::ref_checked = true;
  }
  ddprof::override_symbol(
      T::name, reinterpret_cast<void *>(restore ? T::ref : &T::hook));
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
void setup_overrides() { setup_hooks(false); }
void restore_overrides() { setup_hooks(true); }
} // namespace ddprof