#include <gtest/gtest.h>

#include "savecontext.hpp"
#include "unwind_state.hpp"

#include <array>

#include "regs_convert.hpp"
#include "ddprof_base.hpp"
#include <unwindstack/Arch.h>
#include <unwindstack/Unwinder.h>


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

namespace unwindstack {

TEST(dwarf_unwind, simple) {
  // Load libraries
  pid_t pid = getpid();
  std::shared_ptr<unwindstack::Memory> process_memory;
  process_memory = unwindstack::Memory::CreateProcessMemory(pid);

  std::array<uint64_t, PERF_REGS_COUNT> ddprof_regs;
  size_t size_stack = funcA(ddprof_regs);

  unwindstack::RemoteMaps maps(pid);
  if (!maps.Parse()) {
    printf("Failed to parse maps. \n");
    exit(1);
  }

  x86_64_ucontext_t ucontext = from_regs(ddprof_regs);
  std::unique_ptr<unwindstack::Regs> regs(Regs::CreateFromUcontext(ArchEnum::ARCH_X86_64, &ucontext));

  std::shared_ptr<Memory> mem = Memory::CreateOfflineMemory(
      reinterpret_cast<uint8_t *>(stack),
      ddprof_regs[REGNAME(SP)],
      ddprof_regs[REGNAME(SP)] + size_stack);

  unwindstack::Unwinder unwinder(128, &maps, regs.get(), mem);
  unwinder.Unwind();
  printf("Number of frames = %lu)\n ", unwinder.NumFrames());
  for (auto single_frame : unwinder.frames()) {
    printf("%s \n", unwinder.FormatFrame(single_frame).c_str());
  }
  ASSERT_TRUE(unwinder.NumFrames() > 5);
}

}
