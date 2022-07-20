// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// Architecture mapfile

#ifdef __x86_64__
// Registers 0-11, 16-23
#  define PERF_REGS_COUNT 20
#  define PERF_REGS_MASK 0xff0fff
#  define REGNAME(x) PAM_X86_##x
enum PERF_ARCHMAP_X86 {
  PAM_X86_RAX,
  PAM_X86_RBX,
  PAM_X86_RCX,
  PAM_X86_RDX,
  PAM_X86_RSI,
  PAM_X86_RDI,
  PAM_X86_RBP,
  PAM_X86_RSP,
  PAM_X86_SP = PAM_X86_RSP, // For uniformity
  PAM_X86_RIP,
  PAM_X86_PC = PAM_X86_RIP, // For uniformity
  PAM_X86_FL,
  PAM_X86_CS,
  PAM_X86_SS,
  /*
    PAM_X86_DS,  // These segment registers cannot be read using common user
    PAM_X86_ES,  // permissions.  Accordingly, they are omitted from the mask.
    PAM_X86_FS,  // They are retained here for documentation.
    PAM_X86_GS,  // <-- and this one too
  */
  PAM_X86_R8,
  PAM_X86_R9,
  PAM_X86_R10,
  PAM_X86_R11,
  PAM_X86_R12,
  PAM_X86_R13,
  PAM_X86_R14,
  PAM_X86_R15,
  PAM_X86_MAX,
};
#elif __aarch64__
// Registers 0-32
#  define PERF_REGS_COUNT 33
#  define PERF_REGS_MASK (~(~0ull << PERF_REGS_COUNT))
#  define REGNAME(x) PAM_ARM_##x
enum PERF_ARCHMAP_ARM {
  PAM_ARM_X0,
  PAM_ARM_X1,
  PAM_ARM_X2,
  PAM_ARM_X3,
  PAM_ARM_X4,
  PAM_ARM_X5,
  PAM_ARM_X6,
  PAM_ARM_X7,
  PAM_ARM_X8,
  PAM_ARM_X9,
  PAM_ARM_X10,
  PAM_ARM_X11,
  PAM_ARM_X12,
  PAM_ARM_X13,
  PAM_ARM_X14,
  PAM_ARM_X15,
  PAM_ARM_X16,
  PAM_ARM_X17,
  PAM_ARM_X18,
  PAM_ARM_X19,
  PAM_ARM_X20,
  PAM_ARM_X21,
  PAM_ARM_X22,
  PAM_ARM_X23,
  PAM_ARM_X24,
  PAM_ARM_X25,
  PAM_ARM_X26,
  PAM_ARM_X27,
  PAM_ARM_X28,
  PAM_ARM_X29,
  PAM_ARM_FP = PAM_ARM_X29, // For uniformity
  PAM_ARM_LR,
  PAM_ARM_SP,
  PAM_ARM_PC,
  PAM_ARM_MAX,
};
#else
// cppcheck-suppress preprocessorErrorDirective
#  error Architecture not supported
#endif

#ifdef __cplusplus
extern "C" {
#endif
int param_to_regno(unsigned int param_no);
#ifdef __cplusplus
}
#endif
