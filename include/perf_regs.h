// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
// TODO, this comes from BP, SP, and IP
// see arch/x86/include/uapi/asm/perf_regs.h in the linux sources
// We're going to hardcode everything for now...
#define PERF_REGS_MASK_X86 ((1 << 6) | (1 << 7) | (1 << 8))
#define PERF_REGS_IDX_X86 {6, 7, 8};

// 31 and 32 are the stack and PC, respectively.  29 is r29, see
// https://github.com/ARM-software/abi-aa where it is usd conventionally as the
// frame pointer register
// Note that the order of these has to be changed in the unwinding code!
#define PERF_REGS_MASK_ARM ((1 << 31) | (1 << 32) | (1 << 29))

// This is a human-hardcoded number given the mask above; update it if the mask
// gets more bits
#define PERF_REGS_COUNT 3
#define PERF_REGS_MASK PERF_REGS_MASK_X86
#define PERF_REGS_IDX PERF_REGS_IDX_X86
