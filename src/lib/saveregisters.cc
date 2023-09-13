
// Code taken from https://github.com/libunwind/libunwind
/* libunwind - a platform-independent unwind library
   Copyright (c) 2002-2003 Hewlett-Packard Development Company, L.P.
        Contributed by David Mosberger-Tang <davidm@hpl.hp.com>
   Modified for x86_64 by Max Asbock <masbock@us.ibm.com>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:
The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#include "saveregisters.hpp"

#ifdef __x86_64__

#  define INPUT_REG(reg) [i##reg] "i"(REGNAME(reg))

void save_registers(std::span<uint64_t, PERF_REGS_COUNT>) {
  // The goal here is to capture the state of registers after the return of this
  // function. That is why this function must not be inlined.

  asm(
      // Only save callee saved registers.
      // Caller saved registers are not needed since they could contain anything
      // after function return, and thus cannot be used for unwinding %c[rbx]
      // requires a constant and prints it without punctuation (without it,
      // immediate constant would be printed with a `$` prefix, and this would
      // result in invalid assembly)
      "movq %%rbx, %c[iRBX]*8(%%rdi)\n"
      "movq %%rbp, %c[iRBP]*8(%%rdi)\n"
      "movq %%r12, %c[iR12]*8(%%rdi)\n"
      "movq %%r13, %c[iR13]*8(%%rdi)\n"
      "movq %%r14, %c[iR14]*8(%%rdi)\n"
      "movq %%r15, %c[iR15]*8(%%rdi)\n"
      // Bump the stack by 8 bytes to remove the return address,
      // that way we will have the value of RSP after function return
      "leaq 8(%%rsp), %%rax\n"
      "movq %%rax, %c[iRSP]*8(%%rdi)\n"
      // 0(%rsp) contains the return address, this is the value of RIP after
      // function return
      "movq 0(%%rsp), %%rax\n"
      "movq %%rax, %c[iRIP]*8(%%rdi)\n"
      "ret\n"
      :
      // Pass register indices in input array as input operands
      // INPUT_REG(reg) expands to [i##reg]"i"(PAM_X86_#reg).
      // This allows passing enum value PAM_X86_#reg as an immediate integer
      // constant named i`reg` inside the asm block.
      : INPUT_REG(RBX), INPUT_REG(RBP), INPUT_REG(R12), INPUT_REG(R13),
        INPUT_REG(R14), INPUT_REG(R15), INPUT_REG(RSP), INPUT_REG(RIP)
      :);
}
#endif