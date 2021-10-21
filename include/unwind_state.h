#pragma once

#include <stdbool.h>
#include <sys/types.h>

#include "ddres_def.h"
#include "unwind_output.h"

typedef struct UnwindSymbolsHdr UnwindSymbolsHdr;
typedef struct DsoHdr DsoHdr;
typedef struct DwflHdr DwflHdr;
typedef struct Dwfl Dwfl;

typedef struct UnwindState {
  DwflHdr *dwfl_hdr;
  Dwfl *dwfl;

  DsoHdr *dso_hdr;
  UnwindSymbolsHdr *symbols_hdr;

  pid_t pid;
  char *stack;
  size_t stack_sz;
  union {
    uint64_t regs[3];
    struct {
      uint64_t ebp; // base address of the function's frame
      uint64_t esp; // top of the stack
      uint64_t eip; // Extended Instruction Pointer
    };
  };
  bool attached;
  UnwindOutput output;
} UnwindState;
