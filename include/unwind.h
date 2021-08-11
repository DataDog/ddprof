#ifndef _H_unwind
#define _H_unwind

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

#define MAX_STACK 1024
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
  int max_stack; // ???
  uint64_t ips[MAX_STACK];
  FunLoc locs[MAX_STACK];
  uint64_t idx;
  bool attached;
} UnwindState;

pid_t next_thread(Dwfl *, void *, void **);
bool set_initial_registers(Dwfl_Thread *, void *);
int frame_cb(Dwfl_Frame *, void *);
int tid_cb(Dwfl_Thread *, void *);
void FunLoc_clear(FunLoc *);
DDRes dwfl_caches_clear(struct UnwindState *);
DDRes unwind_init(struct UnwindState *);
void unwind_free(struct UnwindState *);
DDRes unwindstate__unwind(struct UnwindState *us);
void analyze_unwinding_error(pid_t, uint64_t);

#endif
