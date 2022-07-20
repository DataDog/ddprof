// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_archmap.hpp"

// Populate lookups for converting parameter number (1-indexed) to regno
#define R(x) REGNAME(x)
#ifdef __x86_64__
static int reg_lookup[] = {-1, R(RDI), R(RSI), R(RDX), R(RCX), R(R8), R(R9)};
#elif __aarch64__
static int reg_lookup[] = {-1, R(X0), R(X1), R(X2), R(X3), R(X4), R(X5), R(X6)};
#else
// cppcheck-suppress preprocessorErrorDirective
#  error Architecture not supported
#endif
#undef R

int param_to_regno(unsigned int param_no) {
  if (param_no >= sizeof(reg_lookup) / sizeof(*reg_lookup))
    return -1;

  return reg_lookup[param_no];
}
