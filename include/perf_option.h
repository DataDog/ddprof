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

/// Get preset matching index, returns NULL if out of bound
const PerfOption *perfoptions_preset(int idx);

int perfoptions_nb_presets(void);

const char *perfoptions_lookup_idx(int idx);

const char **perfoptions_lookup(void);

/// pure test function
bool perfoptions_match_size(void);
