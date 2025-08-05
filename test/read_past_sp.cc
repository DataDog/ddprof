// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_base.hpp"

#include "chrono_utils.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/time.h>

namespace {
void timer_handler(int /*sig*/) { exit(0); }

DDPROF_NOINLINE __attribute__((naked)) void fun2() {
  asm("pushq  %rbp\n"
      ".cfi_def_cfa_offset 16\n"
      ".cfi_offset 6, -16\n"
      "movq   %rsp, %rbp\n"
      ".cfi_def_cfa_register 6\n"
      "popq   %rbp\n"
      ".cfi_def_cfa 7, 8\n"
      ".label:\n"
      "jmp .label\n"
      "ret\n");
}

DDPROF_NOINLINE void fun1() {
  fun2();
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}
} // namespace

int main() {
  struct sigaction sa{};
  sa.sa_handler = &timer_handler;
  sigaction(SIGPROF, &sa, nullptr);

  constexpr std::chrono::milliseconds timer_delay{200};
  const itimerval val{.it_interval = {},
                      .it_value = ddprof::duration_to_timeval(timer_delay)};
  setitimer(ITIMER_PROF, &val, nullptr);
  fun1();
}
