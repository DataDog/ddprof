#include <gtest/gtest.h>

#include "savecontext.hpp"
#include "unwind_state.hpp"
#include <array>

#include <libunwind.h>

#include "ddprof_base.hpp"

// Retrieves instruction pointer
#define _THIS_IP_                                                              \
  ({                                                                           \
    __label__ __here;                                                          \
  __here:                                                                      \
    (unsigned long)&&__here;                                                   \
  })

// #include "ddprof_defs.hpp"

// temp copy pasta
#define PERF_SAMPLE_STACK_SIZE (4096UL * 8)

std::byte stack[PERF_SAMPLE_STACK_SIZE];

DDPROF_NOINLINE size_t funcA(std::array<uint64_t, PERF_REGS_COUNT> &regs);
DDPROF_NOINLINE size_t funcB(std::array<uint64_t, PERF_REGS_COUNT> &regs);

size_t funcB(std::array<uint64_t, PERF_REGS_COUNT> &regs) {
  printf("Here we are in B %lx \n", _THIS_IP_);
  size_t size = save_context(retrieve_stack_end_address(), regs, stack);

  return size;
}

size_t funcA(std::array<uint64_t, PERF_REGS_COUNT> &regs) {
  printf("Here we are in A %lx \n", _THIS_IP_);
  return funcB(regs);
}

TEST(dwarf_unwind, simple) {
  // Load libraries
  //  CodeCacheArray cache_arary;
  //  Symbols::parseLibraries(&cache_arary, false);
  std::array<uint64_t, PERF_REGS_COUNT> regs;
  size_t size_stack = funcA(regs);
  EXPECT_TRUE(size_stack);



  //  ap::StackContext sc = ap::from_regs(ddprof::span(regs));
  //  ap::StackBuffer buffer(stack, sc.sp, sc.sp + size_stack);

  //  void *callchain[128];
  //  int n = stackWalk(&cache_arary, sc, buffer,
  //                    const_cast<const void **>(callchain), 128, 0);
  //  const char *syms[128];
  //  for (int i = 0; i < n; ++i) {
  //    { // retrieve symbol
  //      CodeCache *code_cache = findLibraryByAddress(
  //          &cache_arary, reinterpret_cast<void *>(callchain[i]));
  //      if (code_cache) {
  //        syms[i] = code_cache->binarySearch(callchain[i]);
  //        printf("IP = %p - %s\n", callchain[i], syms[i]);
  //      }
  //    }
  //  }

  // Check that we found the expected functions during unwinding
  //  ASSERT_TRUE(std::string(syms[0]).find("save_context") !=
  //  std::string::npos); ASSERT_TRUE(std::string(syms[1]).find("funcB") !=
  //  std::string::npos); ASSERT_TRUE(std::string(syms[2]).find("funcA") !=
  //  std::string::npos);
}

#include "allocation_tracker.hpp"
#include "perf_ringbuffer.hpp"
#include "ringbuffer_holder.hpp"
#include "ringbuffer_utils.hpp"
#include "span.hpp"

DDPROF_NOINLINE void func_save_sleep(size_t size);
DDPROF_NOINLINE void func_intermediate_0(size_t size);
DDPROF_NOINLINE void func_intermediate_1(size_t size);

DDPROF_NOINLINE void func_save_sleep(size_t size) {
  int i = 0;
  while (++i < 100000) {
    ddprof::AllocationTracker::track_allocation(0xdeadbeef, size);
    // prevent tail call optimization
    getpid();
    usleep(100);
    //    printf("Save context nb -- %d \n", i);
  }
}

void func_intermediate_0(size_t size) { func_intermediate_1(size); }

void func_intermediate_1(size_t size) { func_save_sleep(size); }

TEST(dwarf_unwind, remote) {
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};
  // use allocation tracking to store events
  ddprof::AllocationTracker::allocation_tracking_init(
      rate, ddprof::AllocationTracker::kDeterministicSampling,
      ring_buffer.get_buffer_info());

  // Fork
  pid_t temp_pid = fork();
  if (!temp_pid) {
    func_intermediate_0(10);
    //    char*const  argList[] = {"sleep", "10", nullptr};
    //    execvp("sleep", argList);
    return;
  }

  // Load libraries from the fork - Cache array is relent to a single pid
  //  CodeCacheArray cache_arary;
  sleep(1);
  //  Symbols::parsePidLibraries(temp_pid, &cache_arary, false);
  // Establish a ring buffer ?

  ddprof::MPSCRingBufferReader reader{ring_buffer.get_ring_buffer()};
  ASSERT_GT(reader.available_size(), 0);

  auto buf = reader.read_sample();
  ASSERT_FALSE(buf.empty());
  const perf_event_header *hdr =
      reinterpret_cast<const perf_event_header *>(buf.data());
  ASSERT_EQ(hdr->type, PERF_RECORD_SAMPLE);

  // convert based on mask for this watcher (default in this case)
  perf_event_sample *sample = hdr2samp(hdr, perf_event_default_sample_type());

  ddprof::span<uint64_t, PERF_REGS_COUNT> regs_span{sample->regs,
                                                    PERF_REGS_COUNT};
  //  ap::StackContext sc = ap::from_regs(regs_span);
  //  ddprof::span<std::byte> stack{
  //      reinterpret_cast<std::byte *>(sample->data_stack),
  //      sample->size_stack};
  //  ap::StackBuffer buffer(stack, sc.sp, sc.sp + sample->size_stack);
  //
  //  void *callchain[DD_MAX_STACK_DEPTH];
  //  int n =
  //      stackWalk(&cache_arary, sc, buffer, const_cast<const void
  //      **>(callchain),
  //                DD_MAX_STACK_DEPTH, 0);

  std::array<const char *, DD_MAX_STACK_DEPTH> syms;
  //  for (int i = 0; i < n; ++i) {
  //    { // retrieve symbol
  //      CodeCache *code_cache = findLibraryByAddress(
  //          &cache_arary, reinterpret_cast<void *>(callchain[i]));
  //      if (code_cache) {
  //        syms[i] = code_cache->binarySearch(callchain[i]);
  //        printf("IP = %p - %s\n", callchain[i], syms[i]);
  //      }
  //    }
  // cleanup the producer fork
  kill(temp_pid, SIGTERM);
//}
}
