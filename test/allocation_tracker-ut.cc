// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "allocation_tracker.hpp"

#include "ddprof_perf_event.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "live_allocation-c.hpp"
#include "loghandle.hpp"
#include "pevent_lib.hpp"
#include "ringbuffer_holder.hpp"
#include "symbol_overrides.hpp"
#include "syscalls.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <cstdlib>
#include <gtest/gtest.h>
#ifdef USE_JEMALLOC
#  include <jemalloc/jemalloc.h>
#endif
#include <malloc.h>
#include <sys/mman.h>
#include <unistd.h>

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
__attribute__((weak)) void *__mmap(void *addr, size_t length, int prot,
                                   int flags, int fd, off_t offset);
__attribute__((weak)) int __munmap(void *addr, size_t length);
}

namespace ddprof {

static const uint64_t k_sampling_rate = 1;
static const size_t k_buf_size_order = 5;

DDPROF_NOINLINE void my_malloc(size_t size, uintptr_t addr = 0xdeadbeef) {
  TrackerThreadLocalState *tl_state = AllocationTracker::get_tl_state();
  ReentryGuard guard(tl_state ? &(tl_state->reentry_guard) : nullptr);
  if (guard) {
    AllocationTracker::track_allocation_s(addr, size, *tl_state);
  }
  // prevent tail call optimization
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

DDPROF_NOINLINE void my_free(uintptr_t addr) {
  TrackerThreadLocalState *tl_state = AllocationTracker::get_tl_state();
  ReentryGuard guard(tl_state ? &(tl_state->reentry_guard) : nullptr);
  if (guard) {
    AllocationTracker::track_deallocation_s(addr, *tl_state);
  }
  // prevent tail call optimization
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

extern "C" {
DDPROF_NOINLINE void my_func_calling_malloc(size_t size) {
  my_malloc(size);
  // prevent tail call optimization
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}
}

TEST(allocation_tracker, start_stop) {
  RingBufferHolder ring_buffer{k_buf_size_order,
                               RingBufferType::kMPSCRingBuffer};
  AllocationTracker::allocation_tracking_init(
      k_sampling_rate,
      AllocationTracker::kDeterministicSampling |
          AllocationTracker::kTrackDeallocations,
      k_default_perf_stack_sample_size, ring_buffer.get_buffer_info());

  defer { AllocationTracker::allocation_tracking_free(); };

  ASSERT_TRUE(AllocationTracker::is_active());
  my_func_calling_malloc(1);
  { // check that we get the relevant info for this allocation
    MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    ASSERT_GT(reader.available_size(), 0);

    auto buf = reader.read_sample();
    ASSERT_FALSE(buf.empty());
    const perf_event_header *hdr =
        reinterpret_cast<const perf_event_header *>(buf.data());
    ASSERT_EQ(hdr->type, PERF_RECORD_SAMPLE);

    perf_event_sample *sample =
        hdr2samp(hdr, perf_event_default_sample_type() | PERF_SAMPLE_ADDR);

    ASSERT_EQ(sample->period, 1);
    ASSERT_EQ(sample->pid, getpid());
    ASSERT_EQ(sample->tid, ddprof::gettid());
    ASSERT_EQ(sample->addr, 0xdeadbeef);

    UnwindState state;
    unwind_init_sample(&state, sample->regs, sample->pid, sample->size_stack,
                       sample->data_stack);
    unwindstate__unwind(&state);

    const auto &symbol_table = state.symbol_hdr._symbol_table;
    ASSERT_GT(state.output.locs.size(), NB_FRAMES_TO_SKIP);
    const auto &symbol =
        symbol_table[state.output.locs[NB_FRAMES_TO_SKIP]._symbol_idx];
    ASSERT_EQ(symbol._symname, "my_func_calling_malloc");
  }
  my_free(0xdeadbeef);
  // ensure we get a deallocation event
  {
    MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    ASSERT_GT(reader.available_size(), 0);

    auto buf = reader.read_sample();
    ASSERT_FALSE(buf.empty());
    const perf_event_header *hdr =
        reinterpret_cast<const perf_event_header *>(buf.data());
    ASSERT_EQ(hdr->type, PERF_CUSTOM_EVENT_DEALLOCATION);
    const DeallocationEvent *sample =
        reinterpret_cast<const DeallocationEvent *>(hdr);
    ASSERT_EQ(sample->ptr, 0xdeadbeef);
  }
  my_free(0xcafebabe);
  {
    MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    ASSERT_EQ(reader.available_size(), 0);
  }
  AllocationTracker::allocation_tracking_free();
  ASSERT_FALSE(AllocationTracker::is_active());
}

TEST(allocation_tracker, stale_lock) {
  LogHandle log_handle;
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  RingBufferHolder ring_buffer{buf_size_order, RingBufferType::kMPSCRingBuffer};
  AllocationTracker::allocation_tracking_init(
      rate,
      AllocationTracker::kDeterministicSampling |
          AllocationTracker::kTrackDeallocations,
      k_default_perf_stack_sample_size, ring_buffer.get_buffer_info());
  defer { AllocationTracker::allocation_tracking_free(); };

  // simulate stale lock
  ring_buffer.get_ring_buffer().spinlock->lock();

  for (uint32_t i = 0; i < AllocationTracker::k_max_consecutive_failures; ++i) {
    TrackerThreadLocalState *tl_state = AllocationTracker::get_tl_state();
    assert(tl_state);
    if (tl_state) {
      AllocationTracker::track_allocation_s(0xdeadbeef, 1, *tl_state);
    }
  }
  ASSERT_FALSE(AllocationTracker::is_active());
}

TEST(allocation_tracker, max_tracked_allocs) {
  RingBufferHolder ring_buffer{k_buf_size_order,
                               RingBufferType::kMPSCRingBuffer};
  AllocationTracker::allocation_tracking_init(
      k_sampling_rate,
      AllocationTracker::kDeterministicSampling |
          AllocationTracker::kTrackDeallocations,
      k_default_perf_stack_sample_size, ring_buffer.get_buffer_info());
  defer { AllocationTracker::allocation_tracking_free(); };

  ASSERT_TRUE(ddprof::AllocationTracker::is_active());
  bool clear_found = false;
  for (int i = 0; i <= ddprof::liveallocation::kMaxTracked + 10; ++i) {
    my_malloc(1, 0x1000 + i);
    ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    while (reader.available_size() > 0) {
      auto buf = reader.read_sample();
      ASSERT_FALSE(buf.empty());
      const perf_event_header *hdr =
          reinterpret_cast<const perf_event_header *>(buf.data());
      if (hdr->type == PERF_RECORD_SAMPLE) {
        perf_event_sample *sample =
            hdr2samp(hdr, perf_event_default_sample_type() | PERF_SAMPLE_ADDR);
        ASSERT_EQ(sample->period, 1);
        ASSERT_EQ(sample->pid, getpid());
        ASSERT_EQ(sample->tid, ddprof::gettid());
        ASSERT_EQ(sample->addr, 0x1000 + i);
      } else {
        if (hdr->type == PERF_CUSTOM_EVENT_CLEAR_LIVE_ALLOCATION) {
          clear_found = true;
        }
      }
    }
  }
  ASSERT_TRUE(clear_found);
}

class AllocFunctionChecker {
public:
  AllocFunctionChecker(RingBuffer &ring_buffer, size_t alloc_size)
      : _ring_buffer(ring_buffer), _alloc_size(alloc_size) {}

  void check_alloc(void *ptr, size_t alloc_size, void **ptr2 = nullptr) {
    MPSCRingBufferReader reader{_ring_buffer};
    ASSERT_GT(reader.available_size(), 0);

    auto buf = reader.read_sample();
    ASSERT_FALSE(buf.empty());
    const perf_event_header *hdr =
        reinterpret_cast<const perf_event_header *>(buf.data());
    ASSERT_EQ(hdr->type, PERF_RECORD_SAMPLE);

    perf_event_sample *sample =
        hdr2samp(hdr, perf_event_default_sample_type() | PERF_SAMPLE_ADDR);

    if (alloc_size > 0) {
      ASSERT_EQ(sample->period, alloc_size);
    }
    ASSERT_EQ(sample->pid, getpid());
    ASSERT_EQ(sample->tid, ddprof::gettid());
    if (ptr) {
      ASSERT_EQ(sample->addr, reinterpret_cast<uintptr_t>(ptr));
    }
    if (ptr2) {
      *ptr2 = (void *)sample->addr;
    }
  }

  void check_dealloc(void *ptr, bool only_last_one = false) {
    MPSCRingBufferReader reader{_ring_buffer};
    ASSERT_GT(reader.available_size(), 0);
    auto buf = reader.read_sample();
    if (only_last_one) {
      while (reader.available_size()) {
        buf = reader.read_sample();
      }
    }
    ASSERT_FALSE(buf.empty());
    const perf_event_header *hdr =
        reinterpret_cast<const perf_event_header *>(buf.data());
    ASSERT_EQ(hdr->type, PERF_CUSTOM_EVENT_DEALLOCATION);

    auto sample = reinterpret_cast<const DeallocationEvent *>(hdr);
    if (ptr) {
      ASSERT_EQ(sample->ptr, reinterpret_cast<uintptr_t>(ptr));
    }
  }

  void check_empty() {
    MPSCRingBufferReader reader{_ring_buffer};
    ASSERT_EQ(reader.available_size(), 0);
  }

  template <typename AllocFunc, typename DeallocFunc>
  DDPROF_NOINLINE void test_alloc(AllocFunc alloc_func,
                                  DeallocFunc dealloc_func,
                                  size_t header_size = 0) {
    empty_ring_buffer();
    try {
      auto ptr = alloc_func(_alloc_size);
      decltype(ptr) ptr2 = decltype(ptr)(((char *)ptr) - header_size);
      check_alloc(ptr2, _alloc_size + header_size);
      check_empty();
      dealloc_func(ptr, _alloc_size + header_size);
      check_dealloc(ptr2);
      check_empty();
    } catch (const std::exception &e) {
      void *ptr;
      check_alloc(nullptr, _alloc_size + header_size, &ptr);
      // There might be many allocations / deallocation for exception handling
      // but the last one should be the deallocation of the initial alloc.
      check_dealloc(ptr, true);
      check_empty();
    }
  }

  template <typename AllocFunc>
  DDPROF_NOINLINE void test_alloc(AllocFunc alloc_func) {
    test_alloc(alloc_func, [](void *ptr, size_t) { ::free(ptr); });
  }

  template <typename AllocFunc, typename ReallocFunc, typename DeallocFunc>
  DDPROF_NOINLINE void test_realloc(AllocFunc alloc_func,
                                    ReallocFunc realloc_func,
                                    DeallocFunc dealloc_func) {
    empty_ring_buffer();
    auto ptr = alloc_func(_alloc_size);
    check_alloc(ptr, _alloc_size);
    check_empty();
    auto new_alloc_size = 2 * _alloc_size;
    auto [ptr2, size2] = realloc_func(ptr, new_alloc_size);
    check_dealloc(ptr);
    check_alloc(ptr2, size2);
    check_empty();
    dealloc_func(ptr2, size2);
    check_dealloc(ptr2);
    check_empty();
  }

  template <typename AllocFunc, typename ReallocFunc>
  DDPROF_NOINLINE void test_realloc(AllocFunc alloc_func,
                                    ReallocFunc realloc_func) {
    test_realloc(alloc_func, realloc_func,
                 [](void *ptr, size_t) { ::free(ptr); });
  }

  void empty_ring_buffer() {
    MPSCRingBufferReader reader{_ring_buffer};
    for (auto buf = reader.read_sample(); !buf.empty();
         buf = reader.read_sample()) {}
  }

private:
  RingBuffer &_ring_buffer;
  size_t _alloc_size;
};

template <typename Func> auto aligned_wrapper(Func func, size_t alignment = 8) {
  return [func, alignment](size_t sz) { return func(alignment, sz); };
}

template <typename Func> auto mmap_wrapper(Func func) {
  return [func](size_t sz) {
    return func(nullptr, sz, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  };
}

template <size_t Size, bool Throw = false, size_t Alignment = 1>
struct TestObj {
  DDPROF_NOINLINE TestObj();
  alignas(Alignment) char buf[Size];
};

// Try to defeat the optimizer by making the constructor (and throw expression)
// noinline.
template <size_t Size, bool Throw, size_t Alignment>
TestObj<Size, Throw, Alignment>::TestObj() {
  if (Throw) {
    throw std::exception{};
  }
}

template <size_t Size, bool Throw = false, size_t Alignment = 1>
struct TestObjWithDestructor {
  DDPROF_NOINLINE TestObjWithDestructor();
  DDPROF_NOINLINE ~TestObjWithDestructor();
  alignas(Alignment) char buf[Size];
};

template <size_t Size, bool Throw, size_t Alignment>
TestObjWithDestructor<Size, Throw, Alignment>::TestObjWithDestructor() {
  if (Throw) {
    throw std::exception{};
  }
}

template <size_t Size, bool Throw, size_t Alignment>
TestObjWithDestructor<Size, Throw, Alignment>::~TestObjWithDestructor() {}

DDPROF_NOINLINE void test_allocation_functions(RingBuffer &ring_buffer) {
  static constexpr size_t alloc_size = 1024;
  AllocFunctionChecker checker{ring_buffer, alloc_size};

  {
    SCOPED_TRACE("malloc/free");
    checker.test_alloc(&::malloc);
  }
  {
    SCOPED_TRACE("calloc/free");
    checker.test_alloc(aligned_wrapper(&::calloc, 1));
  }

#ifndef USE_JEMALLOC
  // jemalloc does not provide pvalloc and freeing a pointer allocated by
  // pvalloc results in a segfault
  if (pvalloc) {
    SCOPED_TRACE("pvalloc/free");
    checker.test_alloc(&::pvalloc);
  }
#else
  {
    SCOPED_TRACE("mallocx/dallocx");
    checker.test_alloc([](size_t sz) { return ::mallocx(sz, 0); },
                       [](void *ptr, size_t sz) { ::dallocx(ptr, 0); });
  }
#  if JEMALLOC_VERSION_MAJOR >= 4
  {
    SCOPED_TRACE("mallocx/sdallocx");
    checker.test_alloc([](size_t sz) { return ::mallocx(sz, 0); },
                       [](void *ptr, size_t sz) { ::sdallocx(ptr, sz, 0); });
  }
#  endif
  {
    SCOPED_TRACE("rallocx/dallocx");
    checker.test_realloc([](size_t sz) { return ::mallocx(sz, 0); },
                         [](void *ptr, size_t sz) {
                           return std::make_pair(::rallocx(ptr, sz, 0), sz);
                         },
                         [](void *ptr, size_t sz) { ::dallocx(ptr, 0); });
  }
  {
    SCOPED_TRACE("xallocx/dallocx");
    checker.test_realloc([](size_t sz) { return ::mallocx(sz, 0); },
                         [](void *ptr, size_t sz) {
                           auto new_size = ::xallocx(ptr, sz, 0, 0);
                           return std::make_pair(ptr, new_size);
                         },
                         [](void *ptr, size_t sz) { ::dallocx(ptr, 0); });
  }
#endif

  {
    SCOPED_TRACE("valloc/free");
    checker.test_alloc(&::valloc);
  }
  {
    SCOPED_TRACE("posix_memalign/free");
    checker.test_alloc([](size_t sz) {
      void *ptr = nullptr;
      EXPECT_EQ(::posix_memalign(&ptr, 8, sz), 0);
      return ptr;
    });
  }
  {
    SCOPED_TRACE("memalign/free");
    checker.test_alloc(aligned_wrapper(&::memalign));
  }
  {
    SCOPED_TRACE("aligned_alloc/free");
    checker.test_alloc(aligned_wrapper(&::aligned_alloc));
  }
  {
    SCOPED_TRACE("realloc/free");
    checker.test_realloc(&::malloc, [](void *ptr, size_t sz) {
      return std::make_pair(::realloc(ptr, sz), sz);
    });
  }
  if (reallocarray) {
    SCOPED_TRACE("realloarray/free");
    checker.test_realloc(&::malloc, [](void *ptr, size_t sz) {
      return std::make_pair(::reallocarray(ptr, 1, sz), sz);
    });
  }

  {
    SCOPED_TRACE("mmap/munmap");
    checker.test_alloc(mmap_wrapper(&::mmap), &::munmap);
  }
  {
    SCOPED_TRACE("mmap64/munmap");
    checker.test_alloc(mmap_wrapper(&::mmap64), &::munmap);
  }
  if (__mmap && __munmap) {
    SCOPED_TRACE("__mmap/__munmap");
    checker.test_alloc(mmap_wrapper(&::__mmap), &::__munmap);
  }

  static constexpr size_t big_align = alignof(std::max_align_t) * 2;
  static constexpr size_t array_size = 16;
  static_assert((alloc_size / array_size) % big_align == 0);

  using Obj = TestObj<alloc_size, false>;
  using AlignedObj = TestObj<alloc_size, false, big_align>;
  using ThrowingObj = TestObj<alloc_size, true>;
  using ThrowingAlignedObj = TestObj<alloc_size, true, big_align>;
  using ArrayObj = TestObj<alloc_size / array_size, false>;
  using AlignedArrayObj = TestObj<alloc_size / array_size, false>;
  using ThrowingArrayObj = TestObj<alloc_size / array_size, true>;
  using ThrowingAlignedArrayObj =
      TestObj<alloc_size / array_size, true, big_align>;

  using NonTrivialArrayObj =
      TestObjWithDestructor<alloc_size / array_size, false>;
  using NonTrivialAlignedArrayObj =
      TestObjWithDestructor<alloc_size / array_size, false, big_align>;
  struct IncompleteObj;

  {
    SCOPED_TRACE("new/delete_sized");
    checker.test_alloc([](size_t) { return new Obj; },
                       [](auto ptr, size_t) { delete ptr; });
  }
  {
    SCOPED_TRACE("new_nothrow/delete");
    checker.test_alloc([](size_t) { return new (std::nothrow) Obj; },
                       [](auto ptr, size_t) { delete ptr; });
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-incomplete"
  // Delete incomplete object to force use of `delete(void* ptr)` overload
  // (otherwise sized version is used)
  {
    SCOPED_TRACE("new/delete");
    checker.test_alloc([](size_t) { return new Obj; },
                       [](auto ptr, size_t) { delete (IncompleteObj *)ptr; });
  }
#pragma GCC diagnostic pop
  {
    SCOPED_TRACE("new/delete_nothrow");
    checker.test_alloc([](size_t) { return new ThrowingObj; },
                       [](auto ptr, size_t) { FAIL(); });
  }
  {
    SCOPED_TRACE("new_nothrow/delete_nothrow");
    checker.test_alloc([](size_t) { return new (std::nothrow) ThrowingObj; },
                       [](auto ptr, size_t) { FAIL(); });
  }

  {
    SCOPED_TRACE("new[]/delete[]");
    checker.test_alloc([](size_t sz) { return new ArrayObj[array_size]; },
                       [](auto ptr, size_t) { delete[] ptr; });
  }

  {
    // When allocating array of non-trivially destructible types, a header
    // containing the number of objects is put before the start of the array.
    SCOPED_TRACE("new[]/delete[]_sized");
    checker.test_alloc(
        [](size_t sz) { return new NonTrivialArrayObj[array_size]; },
        [](auto ptr, size_t) { delete[] ptr; }, sizeof(size_t));
  }
  {
    SCOPED_TRACE("new[]_aligned/delete[]_sized_aligned");
    checker.test_alloc(
        [](size_t sz) { return new NonTrivialAlignedArrayObj[array_size]; },
        [](auto ptr, size_t) { delete[] ptr; }, big_align);
  }
  {
    SCOPED_TRACE("new[]_nothrow/delete[]");
    checker.test_alloc(
        [](size_t sz) { return new (std::nothrow) ArrayObj[array_size]; },
        [](auto ptr, size_t) { delete[] ptr; });
  }
  {
    SCOPED_TRACE("new[]/delete[]_nothrow");
    checker.test_alloc(
        [](size_t sz) { return new ThrowingArrayObj[array_size]; },
        [](auto ptr, size_t) { FAIL(); });
  }
  {
    SCOPED_TRACE("new[]_nothrow/delete[]_nothrow");
    checker.test_alloc(
        [](size_t sz) {
          return new (std::nothrow) ThrowingArrayObj[array_size];
        },
        [](auto ptr, size_t) { FAIL(); });
  }
  {
    SCOPED_TRACE("new_aligned/delete_aligned");
    checker.test_alloc([](size_t) { return new AlignedObj; },
                       [](auto ptr, size_t) { delete ptr; });
  }
  {
    SCOPED_TRACE("new_aligned_nothrow/delete_aligned");
    checker.test_alloc([](size_t) { return new (std::nothrow) AlignedObj; },
                       [](auto ptr, size_t) { delete ptr; });
  }
  {
    SCOPED_TRACE("new[]_aligned/delete[]_aligned");
    checker.test_alloc([](size_t) { return new AlignedArrayObj[array_size]; },
                       [](auto ptr, size_t) { delete[] ptr; });
  }
  {
    SCOPED_TRACE("new[]_aligned_nothrow/delete[]_aligned");
    checker.test_alloc(
        [](size_t) { return new (std::nothrow) AlignedArrayObj[array_size]; },
        [](auto ptr, size_t) { delete[] ptr; });
  }
  {
    SCOPED_TRACE("new_aligned/delete_aligned_nothrow");
    checker.test_alloc([](size_t) { return new ThrowingAlignedObj; },
                       [](auto ptr, size_t) { delete ptr; });
  }
  {
    SCOPED_TRACE("new_aligned_nothrow/delete_aligned_nothrow");
    checker.test_alloc(
        [](size_t) { return new (std::nothrow) ThrowingAlignedObj; },
        [](auto ptr, size_t) { delete ptr; });
  }
  {
    SCOPED_TRACE("new[]_aligned/delete[]_aligned_nothrow");
    checker.test_alloc(
        [](size_t) { return new ThrowingAlignedArrayObj[array_size]; },
        [](auto ptr, size_t) { delete[] ptr; });
  }
  {
    SCOPED_TRACE("new[]_aligned_nothrow/delete[]_aligned_nothrow");
    checker.test_alloc(
        [](size_t) {
          return new (std::nothrow) ThrowingAlignedArrayObj[array_size];
        },
        [](auto ptr, size_t) { delete[] ptr; });
  }
}

TEST(allocation_tracker, test_allocation_functions) {
  RingBufferHolder ring_buffer{k_buf_size_order,
                               RingBufferType::kMPSCRingBuffer};
  AllocationTracker::allocation_tracking_init(
      k_sampling_rate,
      AllocationTracker::kDeterministicSampling |
          AllocationTracker::kTrackDeallocations,
      k_default_perf_stack_sample_size, ring_buffer.get_buffer_info());
  defer { AllocationTracker::allocation_tracking_free(); };

  ASSERT_TRUE(AllocationTracker::is_active());
  ddprof::setup_overrides(std::chrono::milliseconds{0},
                          std::chrono::milliseconds{0});

  // Put all tests in another nmn-inlined functions otherwise compiler may
  // compute `&::malloc` and other allocation function addresses before
  // overrides are setup, and thus effectively calling the true allocation
  // functions instead of hooks. I observed this on clang/ARM, and even a
  // compiler barrier did not prevent the compiler to reorder the computation of
  // these addresses before `setup_overrides`.
  test_allocation_functions(ring_buffer.get_ring_buffer());
  ddprof::restore_overrides();
}

} // namespace ddprof
