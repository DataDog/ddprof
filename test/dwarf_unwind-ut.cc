#include <gtest/gtest.h>


#include "unwind_state.hpp"
#include "savecontext.hpp"
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

#define CAST_TO_VOID_STAR(ptr) reinterpret_cast<void*>(ptr)

std::byte stack[PERF_SAMPLE_STACK_SIZE];

DDPROF_NOINLINE size_t  funcA(std::array <uint64_t, PERF_REGS_COUNT> &regs);
DDPROF_NOINLINE size_t  funcB(std::array <uint64_t, PERF_REGS_COUNT> &regs);

size_t funcB(std::array <uint64_t, PERF_REGS_COUNT> &regs) {
  // Load libraries
  CodeCacheArray cache_arary;
  Symbols::parseLibraries(&cache_arary, false);

  printf("Here we are in B %lx \n", _THIS_IP_);
  size_t size = save_context(retrieve_stack_end_address(), regs, stack);

  { // IP
    uint64_t ip = regs[REGNAME(PC)];
    printf("%lx = ip\n", ip);

    {  // small useless test
      CodeCache *code_cache = findLibraryByAddress(&cache_arary, reinterpret_cast<void*>(ip));
      EXPECT_TRUE(code_cache);
    }
  }

  // context from saving state  
  ap::StackContext sc;
  #ifdef __x86_64__
  sc.pc = CAST_TO_VOID_STAR(regs[REGNAME(PC)]);
  sc.sp = regs[REGNAME(SP)];
  sc.fp = regs[REGNAME(RBP)];
#elif __aarch64__
  sc.pc = CAST_TO_VOID_STAR(regs[REGNAME(PC)]);
  sc.sp = regs[REGNAME(SP)];
  sc.fp = regs[REGNAME(FP)];
#endif
  // size should be < PERF_SAMPLE_STACK_SIZE
  ap::StackBuffer buffer(stack, sc.sp, sc.sp + size);

  void *stack[128];
  int n = stackWalk(&cache_arary, sc, buffer, const_cast<const void**>(stack), 128, 0);
  for (int i = 0; i < n; ++i) {
    { // retrieve symbol
      CodeCache *code_cache = findLibraryByAddress(&cache_arary, reinterpret_cast<void*>(stack[i]));
      if (code_cache) {
        const char *sym_name = code_cache->binarySearch(stack[i]);
        printf("IP = %p - %s\n", stack[i], sym_name);
      }
    }
  }
  
  return size;
}

size_t funcA(std::array <uint64_t, PERF_REGS_COUNT> &regs) {
  printf("Here we are in A %lx \n", _THIS_IP_);
  return funcB(regs);
}


void unwind_async_profiler() {

}

void unwind_libdwfl(){

}

TEST(dwarf_unwind, simple) {
  std::array <uint64_t, PERF_REGS_COUNT> regs;
  size_t  size_stack = funcA(regs);
  EXPECT_TRUE(size_stack);


  // DO REGNAME(RBP) --> Gives the index inside the table
  // DO REGNAME(SP)
  // DO REGNAME(PC)

  // int stackWalk(CodeCacheArray *cache, ap::StackContext &sc, const void** callchain, int max_depth, int skip) {
}
