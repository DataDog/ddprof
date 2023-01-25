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
DDPROF_NOINLINE ddprof::span<const std::byte> retrieve_stack_bounds() {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  if (pthread_getattr_np(pthread_self(), &attrs) != 0) {
    return {};
  }
  defer { pthread_attr_destroy(&attrs); };
  if (pthread_attr_getstack(&attrs, &stack_addr, &stack_size) != 0) {
    return {};
  }
  return {static_cast<std::byte *>(stack_addr),
          static_cast<std::byte *>(stack_addr) + stack_size};
}

// Disable address sanitizer, otherwise it will report a stack-buffer-underflow
// when we are grabbing the stack. But this is not enough, because ASAN
// intercepts memcpy and reports a satck underflow there, empirically it appears
// that both attribute and a suppression are required.
static DDPROF_NO_SANITIZER_ADDRESS size_t
save_stack(ddprof::span<const std::byte> stack_bounds,
           const std::byte *stack_ptr, ddprof::span<std::byte> buffer) {
  // Safety check to ensure we are not in a fiber using a different stack
  if (!(stack_ptr > stack_bounds.begin() && stack_ptr < stack_bounds.end())) {
    return 0;
  }
  // take the min of current stack size and requested stack sample size~
  int64_t saved_stack_size = std::min(static_cast<intptr_t>(buffer.size()),
                                      stack_bounds.end() - stack_ptr);

  if (saved_stack_size <= 0) {
    return 0;
  }
  // Use memmove to stay on the safe side on case caller put `buffer` on the
  // stack
  memmove(buffer.data(), stack_ptr, saved_stack_size);
  return saved_stack_size;
}

size_t save_context(ddprof::span<const std::byte> stack_bounds,
                    ddprof::span<uint64_t, PERF_REGS_COUNT> regs,
                    ddprof::span<std::byte> buffer) {
  save_registers(regs);
  // save the stack just after saving registers, stack part above saved SP
  // must no be changed between call to save_registers and call to save_stack
  return save_stack(stack_bounds,
                    reinterpret_cast<const std::byte *>(regs[REGNAME(SP)]),
                    buffer);
  return 0;
}
