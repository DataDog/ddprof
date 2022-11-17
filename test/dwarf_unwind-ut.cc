#include <gtest/gtest.h>

#include "savecontext.hpp"
#include "unwind_state.hpp"
// #include "symbol.hpp"
#include "stackWalker.h"

#include <array>

#include "async-profiler/codeCache.h"
#include "async-profiler/symbols.h"

#include "async-profiler/stack_context.h"

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

void unwind_async_profiler() {}

void unwind_libdwfl() {}

namespace ap {

}

TEST(dwarf_unwind, simple) {
  // Load libraries
  CodeCacheArray cache_arary;
  Symbols::parseLibraries(&cache_arary, false);
  std::array<uint64_t, PERF_REGS_COUNT> regs;
  size_t size_stack = funcA(regs);
  EXPECT_TRUE(size_stack);

  { // IP
    uint64_t ip = regs[REGNAME(PC)];
    printf("%lx = ip\n", ip);

    { // small useless test
      CodeCache *code_cache =
          findLibraryByAddress(&cache_arary, reinterpret_cast<void *>(ip));
      EXPECT_TRUE(code_cache);
    }
  }
  ap::StackContext sc = ap::from_regs(std::span(regs));
  ap::StackBuffer buffer(stack, sc.sp, sc.sp + size_stack);

  void *stack[128];
  int n = stackWalk(&cache_arary, sc, buffer, const_cast<const void **>(stack),
                    128, 0);
  const char* syms[128];

  for (int i = 0; i < n; ++i) {
    { // retrieve symbol
      CodeCache *code_cache = findLibraryByAddress(
          &cache_arary, reinterpret_cast<void *>(stack[i]));
      if (code_cache) {
        syms[i] = code_cache->binarySearch(stack[i]);
        printf("IP = %p - %s\n", stack[i], syms[i]);
      }
    }
  }
  // Check that we found the expected functions during unwinding
  ASSERT_TRUE(std::string(syms[0]).find("save_context") != std::string::npos);
  ASSERT_TRUE(std::string(syms[1]).find("funcB") != std::string::npos);
  ASSERT_TRUE(std::string(syms[2]).find("funcA") != std::string::npos);
}
