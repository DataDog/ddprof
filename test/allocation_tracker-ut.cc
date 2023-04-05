// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "allocation_tracker.hpp"

#include "ddprof_base.hpp"
#include "ddprof_perf_event.hpp"
#include "ipc.hpp"
#include "loghandle.hpp"
#include "perf_watcher.hpp"
#include "pevent_lib.hpp"
#include "ringbuffer_holder.hpp"
#include "ringbuffer_utils.hpp"
#include "syscalls.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <gtest/gtest.h>
#include <sys/syscall.h>
#include <unistd.h>

DDPROF_NOINLINE void my_malloc(size_t size, uintptr_t addr = 0xdeadbeef) {
  ddprof::AllocationTracker::track_allocation(addr, size);
  // prevent tail call optimization
  getpid();
}

DDPROF_NOINLINE void my_free(uintptr_t addr) {
  ddprof::AllocationTracker::track_deallocation(addr);
  // prevent tail call optimization
  getpid();
}

extern "C" {
DDPROF_NOINLINE void my_func_calling_malloc(size_t size) {
  my_malloc(size);
  // prevent tail call optimization
  getpid();
}
}

#define SOME_TEST
#ifdef SOME_TEST
TEST(allocation_tracker, start_stop) {
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};
  ddprof::AllocationTracker::allocation_tracking_init(
      rate,
      ddprof::AllocationTracker::kDeterministicSampling |
          ddprof::AllocationTracker::kTrackDeallocations,
      ring_buffer.get_buffer_info());

  ASSERT_TRUE(ddprof::AllocationTracker::is_active());
  my_func_calling_malloc(1);
  { // check that we get the relevant info for this allocation
    ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
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
    ddprof::unwind_init_sample(&state, sample->regs, sample->pid,
                               sample->size_stack, sample->data_stack);
    ddprof::unwindstate__unwind(&state);

    const auto &symbol_table = state.symbol_hdr._symbol_table;
    ASSERT_GT(state.output.locs.size(), NB_FRAMES_TO_SKIP);
    const auto &symbol =
        symbol_table[state.output.locs[NB_FRAMES_TO_SKIP]._symbol_idx];
    ASSERT_EQ(symbol._symname, "my_func_calling_malloc");
  }
  my_free(0xdeadbeef);
  // ensure we get a deallocation event
  {
    ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    ASSERT_GT(reader.available_size(), 0);

    auto buf = reader.read_sample();
    ASSERT_FALSE(buf.empty());
    const perf_event_header *hdr =
        reinterpret_cast<const perf_event_header *>(buf.data());
    ASSERT_EQ(hdr->type, PERF_CUSTOM_EVENT_DEALLOCATION);
    const ddprof::DeallocationEvent *sample =
        reinterpret_cast<const ddprof::DeallocationEvent *>(hdr);
    ASSERT_EQ(sample->ptr, 0xdeadbeef);
  }
  my_free(0xcafebabe);
  {
    ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    ASSERT_EQ(reader.available_size(), 0);
  }
  ddprof::AllocationTracker::allocation_tracking_free();
  ASSERT_FALSE(ddprof::AllocationTracker::is_active());
}

TEST(allocation_tracker, stale_lock) {
  LogHandle log_handle;
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};
  ddprof::AllocationTracker::allocation_tracking_init(
      rate,
      ddprof::AllocationTracker::kDeterministicSampling |
          ddprof::AllocationTracker::kTrackDeallocations,
      ring_buffer.get_buffer_info());

  // simulate stale lock
  ring_buffer.get_ring_buffer().spinlock->lock();

  for (uint32_t i = 0;
       i < ddprof::AllocationTracker::k_max_consecutive_failures; ++i) {
    ddprof::AllocationTracker::track_allocation(0xdeadbeef, 1);
  }
  ASSERT_FALSE(ddprof::AllocationTracker::is_active());
  ddprof::AllocationTracker::allocation_tracking_free();
}
#endif

TEST(allocation_tracker, max_tracked_allocs) {
  const uint64_t rate = 1;
  const size_t buf_size_order = 9;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};
  ddprof::AllocationTracker::allocation_tracking_init(
      rate,
      ddprof::AllocationTracker::kDeterministicSampling |
          ddprof::AllocationTracker::kTrackDeallocations,
      ring_buffer.get_buffer_info());

  ASSERT_TRUE(ddprof::AllocationTracker::is_active());

  for (int i = 0; i <= 500001; ++i) {
    my_malloc(1, 0x1000 + i);
    ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
    if (i <=
        500000) { // check that we get the relevant info for this allocation
      printf("Read number -- %u \n", i);
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
      ASSERT_EQ(sample->addr, 0x1000 + i);
    } else {
      {
        auto buf = reader.read_sample();
        ASSERT_FALSE(buf.empty());
        const perf_event_header *hdr =
            reinterpret_cast<const perf_event_header *>(buf.data());
        printf("Type = %u", hdr->type);
      }
      {
        auto buf = reader.read_sample();
        ASSERT_FALSE(buf.empty());
        const perf_event_header *hdr =
            reinterpret_cast<const perf_event_header *>(buf.data());
        printf("Type = %u", hdr->type);
      }
    }
  }
}
