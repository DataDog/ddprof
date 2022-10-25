// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_archmap.hpp"

// F[i] = y_i; where i is the DWARF regno and y_i is the Linux perf regno
unsigned int dwarf_to_perf_regno(unsigned int i) {
  static unsigned int lookup[] = {
#ifdef __x86_64__
      REGNAME(RAX), REGNAME(RDX), REGNAME(RCX), REGNAME(RBX), REGNAME(RSI),
      REGNAME(RDI), REGNAME(RBP), REGNAME(SP),  REGNAME(R8),  REGNAME(R9),
      REGNAME(R10), REGNAME(R11), REGNAME(R12), REGNAME(R13), REGNAME(R14),
      REGNAME(R15), REGNAME(PC),
#elif __aarch64__
      REGNAME(X0),  REGNAME(X1),  REGNAME(X2),  REGNAME(X3),  REGNAME(X4),
      REGNAME(X5),  REGNAME(X6),  REGNAME(X7),  REGNAME(X8),  REGNAME(X9),
      REGNAME(X10), REGNAME(X11), REGNAME(X12), REGNAME(X13), REGNAME(X14),
      REGNAME(X15), REGNAME(X16), REGNAME(X17), REGNAME(X18), REGNAME(X19),
      REGNAME(X20), REGNAME(X21), REGNAME(X22), REGNAME(X23), REGNAME(X24),
      REGNAME(X25), REGNAME(X26), REGNAME(X27), REGNAME(X28), REGNAME(FP),
      REGNAME(LR),  REGNAME(SP),
#else
#  error Architecture not supported
#endif
  };

  if (i >= sizeof(lookup) / sizeof(*lookup)) {
    return -1u; // implicit sentinel value
  }

  return lookup[i];
};

unsigned int param_to_perf_regno(unsigned int param_no) {
// Populate lookups for converting parameter number (1-indexed) to regno
#define R(x) REGNAME(x)
#ifdef __x86_64__
  constexpr int reg_lookup[] = {-1, R(RDI), R(RSI), R(RDX), R(RCX), R(R8), R(R9)};
#elif __aarch64__
  constexpr int reg_lookup[] = {-1, R(X0), R(X1), R(X2), R(X3), R(X4), R(X5), R(X6)};
#else
// cppcheck-suppress preprocessorErrorDirective
#  error Architecture not supported
#endif
#undef R

  if (!param_no || param_no >= sizeof(reg_lookup) / sizeof(*reg_lookup))
    return -1u;

  return reg_lookup[param_no];
}

unsigned int param_to_regno_c(unsigned int n) { return param_to_perf_regno(n); }
