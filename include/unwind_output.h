#pragma once

#include <stdint.h>

#define DD_MAX_STACK 1024

typedef struct FunLoc {
  uint64_t ip;         // Relative to file, not VMA
  uint64_t map_start;  // Start address of mapped region
  uint64_t map_end;    // End
  uint64_t map_off;    // Offset into file
  const char *funname; // name of the function (mangled, possibly)
  const char *srcpath; // name of the source file, if known
  const char
      *sopath;   // name of the file where the symbol is interned (e.g., .so)
  uint32_t line; // line number in file
  uint32_t disc; // discriminator
} FunLoc;

typedef struct UnwindOutput {
  FunLoc locs[DD_MAX_STACK];
  uint64_t idx;
} UnwindOutput;

void uw_output_clear(UnwindOutput *);
