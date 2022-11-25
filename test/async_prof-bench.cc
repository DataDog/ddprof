
#include <benchmark/benchmark.h>

#include "savecontext.hpp"
#include "stackWalker.h"
#include "unwind_state.hpp"

#include "async-profiler/codeCache.h"
#include "async-profiler/stack_context.h"
#include "async-profiler/symbols.h"

#include "allocation_tracker.hpp"
#include "perf_ringbuffer.hpp"
#include "ringbuffer_holder.hpp"
#include "ringbuffer_utils.hpp"
#include "span.hpp"

DDPROF_NOINLINE size_t func_save(std::span<std::byte> stack, std::span<uint64_t, PERF_REGS_COUNT> regs);
DDPROF_NOINLINE size_t func_intermediate_1(int i, std::span<std::byte> stack, std::span<uint64_t, PERF_REGS_COUNT> regs);

DDPROF_NOINLINE size_t func_save(std::span<std::byte> stack, std::span<uint64_t, PERF_REGS_COUNT> regs) {
  return save_context(retrieve_stack_end_address(), regs, stack);
}

DDPROF_NOINLINE size_t func_intermediate_1(int i, std::span<std::byte> stack, std::span<uint64_t, PERF_REGS_COUNT> regs) {
  while(i > 0){
    size_t size = func_intermediate_1(--i, stack, regs);
    DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
    return size;
  }
   size_t size = func_save(stack, regs);
   DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
   return size;

}

static void BM_UnwindSameStack(benchmark::State &state) {
  CodeCacheArray cache_arary;
  Symbols::parseLibraries(&cache_arary, false);

  std::byte stack[PERF_SAMPLE_STACK_SIZE];
  std::array<uint64_t, PERF_REGS_COUNT> regs;

  constexpr int depth_walk = 10;
  // looks like buffer is modified by async profiler
  // I need to save context at all loops
  // This modifies what we are measuring
  size_t size_stack = func_intermediate_1(depth_walk, stack, regs);
  ap::StackContext sc = ap::from_regs(regs);
  ap::StackBuffer buffer(stack, sc.sp, sc.sp + size_stack);

  int cpt = 0;
  for (auto _ : state) {
    ap::StackContext sc = ap::from_regs(regs);

    void *callchain[DD_MAX_STACK_DEPTH];
    int n =
        stackWalk(&cache_arary, sc, buffer,
                  const_cast<const void **>(callchain), DD_MAX_STACK_DEPTH, 0);
    if (unlikely(n < depth_walk)) {
      printf("n = %d \n", n);
      exit(1);
    }

    const char *syms[128];
    for (int i = 0; i < n; ++i) {
      { // retrieve symbol
        CodeCache *code_cache = findLibraryByAddress(
            &cache_arary, reinterpret_cast<void *>(callchain[i]));
        if (likely(code_cache)) {
          syms[i] = code_cache->binarySearch(callchain[i]);
//          printf("IP[%d] = %p - %s\n", i, callchain[i], syms[i]);
        }
        else {
          printf("error");
          exit(1);
        }
        cpt += strlen(syms[i]);
      }
    }
  }
  printf("total cpt = %d \n", cpt);
}

BENCHMARK(BM_UnwindSameStack);
