#pragma once

#include "libdw.h"
#include "libdwfl.h"
#include "libebl.h"
#include <dwarf.h>
#include <stdbool.h>

#include "ddres_def.h"
#include "dso.h"
#include "dwfl_internals.h"
#include "dwfl_module_cache.h"
#include "procutils.h"
#include "unwind_output.h"

typedef struct UnwindState {
  Dwfl *dwfl;
  struct dwflmod_cache_hdr *cache_hdr;
  pid_t pid;
  char *stack;
  size_t stack_sz;
  union {
    uint64_t regs[3];
    struct {
      uint64_t ebp;
      uint64_t esp;
      uint64_t eip;
    };
  };
  Dso *dso;
  bool attached;
  UnwindOutput output;
} UnwindState;

pid_t next_thread(Dwfl *, void *, void **);
bool set_initial_registers(Dwfl_Thread *, void *);
int frame_cb(Dwfl_Frame *, void *);
int tid_cb(Dwfl_Thread *, void *);
DDRes dwfl_caches_clear(struct UnwindState *);
DDRes unwind_init(struct UnwindState *);
void unwind_free(struct UnwindState *);
DDRes unwindstate__unwind(struct UnwindState *us);
void analyze_unwinding_error(pid_t, uint64_t);
