// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PERF_REGS_MAX 8 // defines max storage for registers in options
// e.g., rdi, rsi, rdx, rcx, r8, r9
// Further parameters are stack-allocated
// Taken from arch/x86/include/uapi/asm/perf_regs.h for consistency with perf
// (but we don't want to pull in kernel sources)
typedef enum po_cpuregs_t {
  PO_CPU_RDI = 5,
  PO_CPU_RSI = 4,
  PO_CPU_RDX = 3,
  PO_CPU_RCX = 2,
  PO_CPU_R08 = 16,
  PO_CPU_R09 = 17,
  PO_ARG1 = PO_CPU_RDI, // For comfort
  PO_ARG2 = PO_CPU_RSI,
  PO_ARG3 = PO_CPU_RDX,
  PO_ARG4 = PO_CPU_RCX,
  PO_ARG5 = PO_CPU_R08,
  PO_ARG6 = PO_CPU_R09,
} po_cpuregs_t;

typedef struct BPDef {
  uint64_t bp_addr;
  uint64_t bp_len;
} BPDef;

typedef struct PerfOption {
  const char *desc;
  int type;
  union {
    unsigned long config;
    BPDef *bp;
  };
  union {
    uint64_t sample_period;
    uint64_t sample_frequency;
  };
  const char *label;
  const char *unit;
  int mode;
  bool include_kernel;
  bool freq;
  char bp_type;
  uint64_t regmask;
  uint64_t regs_idx[PERF_REGS_MAX];
  po_cpuregs_t target_reg;
} PerfOption;

/// Get preset matching index, returns NULL if out of bound
const PerfOption *perfoptions_preset(int idx);

int perfoptions_nb_presets(void);

const char *perfoptions_lookup_idx(int idx);

const char **perfoptions_lookup(void);

/// pure test function
bool perfoptions_match_size(void);
