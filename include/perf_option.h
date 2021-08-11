#pragma once

#include <stdbool.h>
#include <stdint.h>

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
} PerfOption;
