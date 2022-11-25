
#include <benchmark/benchmark.h>

#include "savecontext.hpp"
#include "unwind_state.hpp"

#include <array>

#include "ddprof_base.hpp"
#include "regs_convert.hpp"

#include <unwindstack/Arch.h>
#include <unwindstack/Unwinder.h>

#include <unwindstack/RegsX86_64.h>
#include <unwindstack/UserX86_64.h>
#include <unwindstack/UcontextX86_64.h>

//libunwindstack-bench.cc

DDPROF_NOINLINE size_t func_save(ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs);
DDPROF_NOINLINE size_t func_intermediate_1(int i, ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs);

DDPROF_NOINLINE size_t func_save(ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs) {
  static thread_local size_t tl_size = 0;
  if (!tl_size) {
    tl_size = save_context(retrieve_stack_end_address(), regs, stack);
  }
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
  return tl_size;
}

DDPROF_NOINLINE size_t func_intermediate_1(int i, ddprof::span<std::byte> stack, ddprof::span<uint64_t, PERF_REGS_COUNT> regs) {
  while(i > 0){
    size_t size = func_intermediate_1(--i, stack, regs);
    DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
    return size;
  }
  size_t size = func_save(stack, regs);
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
  return size;

}

using namespace unwindstack;

static void BM_UnwindSameStack(benchmark::State &state) {
  std::byte stack[PERF_SAMPLE_STACK_SIZE];
  std::array<uint64_t, PERF_REGS_COUNT> ddprof_regs;

  constexpr int depth_walk = 10;

  unwindstack::Elf::SetCachingEnabled(false);

  unwindstack::RemoteMaps maps(getpid());
  if (!maps.Parse()) {
    printf("Failed to parse maps. \n");
    exit(1);
  }


  int cpt = 0;
  for (auto _ : state) {
    size_t size_stack = func_intermediate_1(depth_walk, stack, ddprof_regs);
    x86_64_ucontext_t ucontext = from_regs(ddprof_regs);
    std::shared_ptr<Memory> mem = Memory::CreateOfflineMemory(
        reinterpret_cast<uint8_t *>(stack),
        ddprof_regs[REGNAME(SP)],
        ddprof_regs[REGNAME(SP)] + size_stack);

    std::unique_ptr<unwindstack::Regs> regs(Regs::CreateFromUcontext(ArchEnum::ARCH_X86_64, &ucontext));

    unwindstack::Unwinder unwinder(128, &maps, regs.get(), mem);
    unwinder.SetResolveNames(true);
    unwinder.Unwind();

    if (unlikely(unwinder.NumFrames() < depth_walk)) {
      printf("n = %d \n", unwinder.NumFrames());
      exit(1);
    }

    for (auto single_frame : unwinder.frames()) {
//      printf("%s \n", unwinder.FormatFrame(single_frame).c_str());
      cpt += single_frame.function_name.operator std::string_view().size();
    }
  }
  printf("cpt = %d \n", cpt);
}

BENCHMARK(BM_UnwindSameStack);
