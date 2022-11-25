#pragma once

#include "span.hpp"
#include "perf_archmap.hpp"

#ifdef __x86_64__
#include <unwindstack/RegsX86_64.h>
#include <unwindstack/UserX86_64.h>
#include <unwindstack/UcontextX86_64.h>

namespace unwindstack {

static x86_64_ucontext_t from_regs(ddprof::span<uint64_t, PERF_REGS_COUNT> regs){
  x86_64_ucontext_t ucontext = {};
  x86_64_mcontext_t &mcontext = ucontext.uc_mcontext;
  mcontext.r15 = regs[REGNAME(R15)];
  mcontext.r14 = regs[REGNAME(R14)];
  mcontext.r13 = regs[REGNAME(R13)];
  mcontext.r12 = regs[REGNAME(R12)];
  mcontext.r11 = regs[REGNAME(R11)];
  mcontext.r10 = regs[REGNAME(R10)];
  mcontext.r9 = regs[REGNAME(R9)];
  mcontext.r8 = regs[REGNAME(R8)];
  mcontext.rax = regs[REGNAME(RAX)];
  mcontext.rcx = regs[REGNAME(RCX)];
  mcontext.rdx = regs[REGNAME(RDX)];
  mcontext.rsi = regs[REGNAME(RSI)];
  mcontext.rdi = regs[REGNAME(RDI)];
  mcontext.rbp = regs[REGNAME(RBP)];
  mcontext.rip = regs[REGNAME(RIP)];
  mcontext.efl = regs[REGNAME(FL)];

  // I'm somewhat unsure about how relevant the CS register is
  //  user_regs.csgsfs = regs[REGNAME(CS)];
  mcontext.rsp = regs[REGNAME(RSP)];
  return ucontext;
}
#elif __aarch64__
#error I did not implement this
#endif

}
