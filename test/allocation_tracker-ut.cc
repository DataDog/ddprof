// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "allocation_tracker.hpp"

#include "ddprof_base.hpp"
#include "ipc.hpp"
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

DDPROF_NOINLINE void my_malloc(size_t size) {
  ddprof::AllocationTracker::track_allocation(0xdeadbeef, size);
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

TEST(allocation_tracker, start_stop) {
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};
  ddprof::AllocationTracker::allocation_tracking_init(
      rate, ddprof::AllocationTracker::kDeterministicSampling,
      ring_buffer.get_buffer_info());

  my_func_calling_malloc(1);

  ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
  ASSERT_GT(reader.available_size(), 0);

  auto buf = reader.read_sample();
  ASSERT_FALSE(buf.empty());
  const perf_event_header *hdr =
      reinterpret_cast<const perf_event_header *>(buf.data());
  ASSERT_EQ(hdr->type, PERF_RECORD_SAMPLE);

  perf_event_sample *sample = hdr2samp(hdr, perf_event_default_sample_type());

  ASSERT_EQ(sample->period, 1);
  ASSERT_EQ(sample->pid, getpid());
  ASSERT_EQ(sample->tid, ddprof::gettid());

  UnwindState state;
  ddprof::unwind_init_sample(&state, sample->regs, sample->pid,
                             sample->size_stack, sample->data_stack);
  ddprof::unwindstate__unwind(&state);

  const auto &symbol_table = state.symbol_hdr._symbol_table;
  ASSERT_GT(state.output.nb_locs, NB_FRAMES_TO_SKIP);
  const auto &symbol =
      symbol_table[state.output.locs[NB_FRAMES_TO_SKIP]._symbol_idx];
  ASSERT_EQ(symbol._symname, "my_func_calling_malloc");

  ddprof::AllocationTracker::allocation_tracking_free();
}
