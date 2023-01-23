// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "savecontext.hpp"

#include "ddprof_base.hpp"
#include "defer.hpp"
#include "saveregisters.hpp"
#include "unlikely.hpp"

#include <algorithm>
#include <cstring>
#include <pthread.h>

// Returns -1 in case of failure
// Fills start (low address, so closer to SP) and end (stack end address is the
// start of the stack since stack grows down)
DDPROF_NOINLINE int retrieve_stack_bounds(const std::byte *&start,
                                          const std::byte *&end) {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  if (pthread_getattr_np(pthread_self(), &attrs) != 0) {
    return -1;
  }
  defer { pthread_attr_destroy(&attrs); };
  if (pthread_attr_getstack(&attrs, &stack_addr, &stack_size) != 0) {
    return -1;
  }
  start = static_cast<std::byte *>(stack_addr);
  end = static_cast<std::byte *>(stack_addr) + stack_size;
  return 0;
}

// Disable address sanitizer, otherwise it will report a stack-buffer-underflow
// when we are grabbing the stack. But this is not enough, because ASAN
// intercepts memcpy and reports a satck underflow there, empirically it appears
// that both attribute and a suppression are required.
static DDPROF_NO_SANITIZER_ADDRESS size_t
save_stack(const std::byte *stack_end, const std::byte *stack_ptr,
           ddprof::span<std::byte> buffer) {
  // take the min of current stack size and requested stack sample size~
  int64_t saved_stack_size =
      std::min(static_cast<intptr_t>(buffer.size()), stack_end - stack_ptr);

  if (saved_stack_size <= 0) {
    return 0;
  }
  // Use memmove to stay on the safe side on case caller put `buffer` on the
  // stack
  memmove(buffer.data(), stack_ptr, saved_stack_size);
  return saved_stack_size;
}

size_t save_context(const std::byte *stack_start, const std::byte *stack_end,
                    ddprof::span<uint64_t, PERF_REGS_COUNT> regs,
                    ddprof::span<std::byte> buffer) {
  save_registers(regs);
  // Safety check to ensure we are not in a fiber using a different stack
  if (reinterpret_cast<const std::byte *>(regs[REGNAME(SP)]) > stack_start &&
      reinterpret_cast<const std::byte *>(regs[REGNAME(SP)]) < stack_end) {
    // save the stack just after saving registers, stack part above saved SP
    // must no be changed between call to save_registers and call to save_stack
    return save_stack(stack_end,
                      reinterpret_cast<const std::byte *>(regs[REGNAME(SP)]),
                      buffer);
  }
  return 0;
}
