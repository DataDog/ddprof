#include <gtest/gtest.h>


#include "unwind_state.hpp"
#include "savecontext.hpp"

// #include "ddprof_defs.hpp"

// temp copy pasta
#define PERF_SAMPLE_STACK_SIZE (4096UL * 8)

std::byte stack[PERF_SAMPLE_STACK_SIZE];


TEST(dwarf_unwind, simple) {
  uint64_t regs[K_NB_REGS_UNWIND];
  size_t stack_size = save_context(retrieve_stack_end_address(), regs, stack);
  // DO REGNAME(RBP) --> Gives the index inside the table
  // DO REGNAME(SP)
  // DO REGNAME(PC)

}
