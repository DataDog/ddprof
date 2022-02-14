// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "perf_regs.h"

#define PERF_REGS_MAX 8 // defines max storage for registers in options

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
  uint8_t target_reg; // register number of the target
  uint8_t target_reg_idx; // index in the register array of the target
  uint8_t regs_idx[PERF_REGS_MAX];
} PerfOption;

/// Get preset matching index, returns NULL if out of bound
const PerfOption *perfoptions_preset(int idx);

int perfoptions_get_tracepoint_idx();

int perfoptions_nb_presets(void);

const char *perfoptions_lookup_idx(int idx);

const char **perfoptions_lookup(void);

/// pure test function
bool perfoptions_match_size(void);
