#pragma once

#include <stdint.h>

#include "string_view.h"

#define DD_MAX_STACK 1024

typedef struct FunLoc {
  uint64_t ip;         // Relative to file, not VMA
  uint64_t map_start;  // Start address of mapped region
  uint64_t map_end;    // End
  uint64_t map_off;    // Offset into file
  string_view funname; // name of the function (mangled, possibly)
  string_view srcpath; // name of the source file, if known
  string_view sopath;  // name of the file where the symbol resides (e.g. .so)
  uint32_t line;       // line number in file
  uint32_t disc;       // discriminator
} FunLoc;

typedef struct UnwindOutput {
  FunLoc locs[DD_MAX_STACK];
  uint64_t idx;
} UnwindOutput;

void uw_output_clear(UnwindOutput *);
