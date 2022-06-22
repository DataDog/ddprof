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

#pragma once

#include "perf_archmap.hpp"
#include "span.hpp"

#if defined(__x86_64__)

__attribute__((noinline, naked)) void
    save_registers(ddprof::span<uint64_t, PERF_REGS_COUNT>);

#elif defined(__aarch64__)

#  define save_registers(regs)                                                 \
    ({                                                                         \
      register uint64_t base_ptr __asm__("x0") = (uint64_t)(regs).data();      \
      __asm__ __volatile__("stp x0, x1, [%[base], #0]\n"                       \
                           "stp x2, x3, [%[base], #16]\n"                      \
                           "stp x4, x5, [%[base], #32]\n"                      \
                           "stp x6, x7, [%[base], #48]\n"                      \
                           "stp x8, x9, [%[base], #64]\n"                      \
                           "stp x10, x11, [%[base], #80]\n"                    \
                           "stp x12, x13, [%[base], #96]\n"                    \
                           "stp x14, x13, [%[base], #112]\n"                   \
                           "stp x16, x17, [%[base], #128]\n"                   \
                           "stp x18, x19, [%[base], #144]\n"                   \
                           "stp x20, x21, [%[base], #160]\n"                   \
                           "stp x22, x23, [%[base], #176]\n"                   \
                           "stp x24, x25, [%[base], #192]\n"                   \
                           "stp x26, x27, [%[base], #208]\n"                   \
                           "stp x28, x29, [%[base], #224]\n"                   \
                           "mov x1, sp\n"                                      \
                           "stp x30, x1, [%[base], #240]\n"                    \
                           "adr x1, ret%=\n"                                   \
                           "str x1, [%[base], #256]\n"                         \
                           "mov %[base], #0\n"                                 \
                           "ret%=:\n"                                          \
                           : [base] "+r"(base_ptr)                             \
                           :                                                   \
                           : "x1", "memory");                                  \
    })

#endif
