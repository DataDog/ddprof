#include <benchmark/benchmark.h>


#include "lib/savecontext.hpp"
#include "unwind_state.hpp"
#include "span.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include "ddprof_base.hpp"
#include <gtest/gtest.h>

#include "span.hpp"
#include <span>

DDPROF_NOINLINE size_t func_save(ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs);
DDPROF_NOINLINE size_t func_intermediate_1(int i, ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs);

DDPROF_NOINLINE size_t func_save(ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs) {
  return save_context(retrieve_stack_end_address(), regs, stack);
}

DDPROF_NOINLINE size_t func_intermediate_1(int i, ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs) {
  while(i > 0){
    return func_intermediate_1(--i, stack, regs);
  }
  return func_save(stack, regs);
}

static void BM_SaveContext(benchmark::State &state) {
  UnwindState unwind_state;
  std::byte stack[PERF_SAMPLE_STACK_SIZE];
  std::array<uint64_t, PERF_REGS_COUNT> regs;

  constexpr int depth_walk = 10;
  pid_t mypid = getpid();
  int cpt = 0;
  for (auto _ : state) {
    // looks like buffer is modified by async profiler
    // I need to save context at all loops
    // This slightly modifies the bench
    size_t size_stack = func_intermediate_1(depth_walk, stack, regs);
    ddprof::unwind_init_sample(&unwind_state, regs.data(), mypid,
                               size_stack, reinterpret_cast<char*>(stack));
    ddprof::unwindstate__unwind(&unwind_state);

    if (unlikely(unwind_state.output.nb_locs < depth_walk)) {
      printf("Exit as unwind output = %d \n", unwind_state.output.nb_locs);
//      exit(1);
    }
    const auto &symbol_table = unwind_state.symbol_hdr._symbol_table;

    for (int i = 0; i < unwind_state.output.nb_locs; ++i) {
      {
        const auto &symbol =
            symbol_table[unwind_state.output.locs[i]._symbol_idx];
//        printf("IP[%d] = %s\n", i, symbol._demangle_name.c_str());
        cpt += symbol._demangle_name.size();
      }
    }
  }
  printf("cpt = %d", cpt);
}

BENCHMARK(BM_SaveContext);
