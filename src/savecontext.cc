// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "savecontext.hpp"

#include <algorithm>
#include <cstring>
#include <pthread.h>

#define XSTR(s) STR(s)
#define STR(s) #s

#ifdef __x86_64__
// hardcode register index in mask
// Did not find a way to reuse enum values from perf-archmap.h
#  define RBX_IDX 1
#  define RBP_IDX 6
#  define RSP_IDX 7
#  define RIP_IDX 8
#  define R12_IDX 16
#  define R13_IDX 17
#  define R14_IDX 18
#  define R15_IDX 19

#  define DEST_MEM(reg) XSTR(reg##_IDX) "*8(%rdi)\n"
#  define SAVE_REG(reg) "movq %" #  reg ", " DEST_MEM(reg)

static __attribute__((noinline, naked)) void
save_registers(ddprof::span<uint64_t, PERF_REGS_COUNT>) {
  // The goal here is to capture the state of registers after the return of this
  // function. That is why this function must not be inlined.

  // clang-format off
    asm (
    // Retrieve caller save registers
    // Callee save register are not needed since they could contain anything after function return
    SAVE_REG(RBX)
    SAVE_REG(RBP)    
    SAVE_REG(R12)
    SAVE_REG(R13)
    SAVE_REG(R14)
    SAVE_REG(R15)
    // Bump the stack by 8 bytes to remove the return address, 
    // that way we will have the value of RSP after funtion return
    "leaq 8(%rsp), %rax\n"
    "movq %rax, " DEST_MEM(RSP) "\n"
    // 0(%rsp) contains the return address, this is the value of RIP after funtion return
    "movq 0(%rsp), %rax\n"
    "movq %rax, " DEST_MEM(RIP) "\n"
    "ret\n"
    );
  // clang-format on
}

#else

static __attribute__((noinline, naked)) void
save_registers(ddprof::span<uint64_t, PERF_REGS_COUNT>) {
  asm("ret\n");
}

#endif

// Return stack end address (stack end address is the start of the stack since
// stack grows down)
static __attribute__((noinline)) const std::byte* get_stack_end_address() {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  pthread_getattr_np(pthread_self(), &attrs);
  pthread_attr_getstack(&attrs, &stack_addr, &stack_size);
  pthread_attr_destroy(&attrs);
  return static_cast<std::byte *>(stack_addr) + stack_size;
}

// Disable address sanitizer, otherwise it will report a stack-buffer-underflow
// when we are grabbing the stack. But this is not enough, because ASAN
// intercepts memcpy and reports a satck underflow there, empirically it appears
// that both attribute and a suppression are required.
static __attribute__((no_sanitize("address"))) size_t
save_stack(const std::byte *stack_ptr, ddprof::span<std::byte> buffer) {
  // Use a thread local variable to cache the stack end address per thread
  thread_local static const std::byte *stack_end{};

  if (!stack_end) {
    // Slow path, takes ~30us
    stack_end = get_stack_end_address();
  }

  // take the min of current stack size and requested stack sample size
  size_t saved_stack_size =
      std::min(static_cast<intptr_t>(buffer.size()), stack_end - stack_ptr);
  // Use memmove to stay on the safe side on case caller put `buffer` on the
  // stack
  memmove(buffer.data(), stack_ptr, saved_stack_size);
  return saved_stack_size;
}

size_t save_context(ddprof::span<uint64_t, PERF_REGS_COUNT> regs,
                    ddprof::span<std::byte> buffer) {
  save_registers(regs);
  // save the stack just after saving registers, stack part above saved RSP must
  // no be changed between call to save_registers and call to save_stack
  return save_stack(reinterpret_cast<const std::byte *>(regs[REGNAME(SP)]),
                    buffer);
}
