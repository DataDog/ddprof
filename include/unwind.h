#ifndef _H_unwind
#define _H_unwind

#include "libdw.h"
#include "libdwfl.h"
#include "libebl.h"
#include <dwarf.h>
#include <stdbool.h>

#include "demangle.h"
#include "dso.h"
#include "dwfl_internals.h"
#include "logger.h"
#include "procutils.h"
#include "signal_helper.h"

#define UNUSED(x) (void)(x)

#define MAX_STACK 1024
typedef struct FunLoc {
  uint64_t ip;        // Relative to file, not VMA
  uint64_t map_start; // Start address of mapped region
  uint64_t map_end;   // End
  uint64_t map_off;   // Offset into file
  char *funname;      // name of the function (mangled, possibly)
  char *srcpath;      // name of the source file, if known
  char *sopath;  // name of the file where the symbol is interned (e.g., .so)
  uint32_t line; // line number in file
  uint32_t disc; // discriminator
} FunLoc;

typedef struct UnwindState {
  Dwfl *dwfl;
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
} UnwindState;

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  (void)dwfl;
  if (*thread_argp != NULL) {
    return 0;
  }
  struct UnwindState *us = arg;
  *thread_argp = arg;
  return us->pid;
}

bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  struct UnwindState *us = arg;
  Dwarf_Word regs[17] = {0};

  // I only save three lol
  regs[6] = us->ebp;
  regs[7] = us->esp;
  regs[16] = us->eip;

  return dwfl_thread_state_registers(thread, 0, 17, regs);
}

bool memory_read(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg) {
  (void)dwfl;
  struct UnwindState *us = arg;

  uint64_t sp_start = us->esp;
  uint64_t sp_end = sp_start + us->stack_sz;

  // Check for overflow, like perf
  if (addr + sizeof(Dwarf_Word) < addr) {
    return false;
  }

  if (addr < sp_start || addr + sizeof(Dwarf_Word) > sp_end) {
    // If we're here, we're not in the stack.  We should interpet addr as an
    // address in VM, not as a file offset.
    // Strongly assumes we're also in an executable region?
    bool ret = pid_read_dso(us->pid, result, sizeof(Dwarf_Word), addr);
    if (!ret) {
      LG_NTC("[UNWIND] Couldn't get read 0x%lx from %d" addr, us->pid);
      LG_NTC("[UNWIND] stack is 0x%lx:0x%lx", sp_start, sp_end);
    }
    return ret;
  }

  if (addr >= sp_end) {
    LG_NTC("[UNWIND] Address might be in stack, but exceeds buffer");
    return false;
  }

  // We're probably safe to read
  *result = *(Dwarf_Word *)(&us->stack[addr - sp_start]);
  return true;
}

int frame_cb(Dwfl_Frame *state, void *arg) {
  struct UnwindState *us = arg;
  Dwarf_Addr pc = 0;
  bool isactivation = false;

  if (!dwfl_frame_pc(state, &pc, &isactivation)) {
    LG_WRN("[UNWIND] %s", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }

  // If this is not an activation frame, then it was a frame which was in an op
  // like CALL.  The convention is to advance the stored IP past the CALL, so on
  // return there is no ambiguity about the state of the IP.  That means, for
  // the purpose of unwinding, the stated value of the IP is misleading and must
  // be corrected.  Formally, I sense that this needs to be decremented back
  // to the CALL, but I see a lot of code merely decrementing by one, so we
  // follow that convention here.  DAS - TODO am I missing something?
  if (!isactivation)
    --pc;

  Dwfl_Thread *thread = dwfl_frame_thread(state);
  if (!thread) {
    LG_WRN("[UNWIND] dwfl_frame_thread was zero: (%s)", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }
  Dwfl *dwfl = dwfl_thread_dwfl(thread);
  if (!dwfl) {
    LG_WRN("[UNWIND] dwfl_thread_dwfl was zero: (%s)", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }
  Dwfl_Module *mod = dwfl_addrmodule(dwfl, pc);
  if (!mod) {
    LG_WRN("[UNWIND] dwfl_addrmodule for 0x:%lx was zero: (%s)", pc,
           dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }

  // Now we register
  const char *symname = NULL;
  GElf_Off offset = {0};
  GElf_Sym sym = {0};
  GElf_Word shndxp = {0};
  Elf *elfp = NULL;
  Dwarf_Addr bias = {0};

  symname = dwfl_module_addrinfo(mod, pc, &offset, &sym, &shndxp, &elfp, &bias);

  Dwfl_Line *line = dwfl_module_getsrc(mod, pc);

  // TODO
  us->locs[us->idx].ip = pc;

  int lineno = 0;
  us->locs[us->idx].line = 0;
  const char *srcpath = dwfl_lineinfo(line, &pc, &lineno, 0, 0, 0);
  if (srcpath) {
    us->locs[us->idx].srcpath = strdup(srcpath);
    us->locs[us->idx].line = lineno;
  }

  // Figure out mapping
  Dso *dso = dso_find(us->pid, pc);

  // If we failed, then try backpopulating and do it again
  if (!dso) {
    LG_WRN("[UNWIND] Failed to locate DSO for [%d] 0x%lx, get", us->pid, pc);
    pid_backpopulate(us->pid);
    if ((dso = dso_find(us->pid, pc)))
      LG_DBG("[UNWIND] Located DSO (%s)", dso_path(dso));
    else
      pid_find_ip(us->pid, pc);
  }
  if (dso) {
    us->locs[us->idx].map_start = dso->start;
    us->locs[us->idx].map_end = dso->end;
    us->locs[us->idx].map_off = dso->pgoff;
    us->locs[us->idx].sopath = strdup(dso_path(dso));
  } else {
    // Try to rely on the data we have at hand, but it's certainly wrong
    LG_WRN("[UNWIND] Failed to locate DSO for [%d] 0x%lx again", us->pid, pc);
    pid_find_ip(us->pid, pc);
    us->locs[us->idx].map_start = mod->low_addr;
    us->locs[us->idx].map_end = mod->high_addr;
    us->locs[us->idx].map_off = offset;
    char *sname = strrchr(mod->name, '/');
    us->locs[us->idx].sopath = strdup(sname ? sname + 1 : mod->name);
    LG_DBG("[UNWIND] Located DSO at path %s", us->locs[us->idx].sopath);
  }

  char tmpname[1024];
  if (symname) {
    demangle(symname, tmpname, sizeof(tmpname) / sizeof(*tmpname));
    us->locs[us->idx].funname = strdup(tmpname);
  } else {
    snprintf(tmpname, 1016, "0x%lx", mod->low_addr);
    us->locs[us->idx].funname = strdup(tmpname);
  }

  LG_DBG("[UNWIND] (%s):(%d)", us->locs[us->idx].funname,
         us->locs[us->idx].line);
  us->idx++;
  return DWARF_CB_OK;
}

int tid_cb(Dwfl_Thread *thread, void *targ) {
  dwfl_thread_getframes(thread, frame_cb, targ);
  return DWARF_CB_OK;
}

void FunLoc_clear(FunLoc *locs) {
  for (int i = 0; i < MAX_STACK; i++) {
    free(locs[i].funname);
    free(locs[i].sopath);
    free(locs[i].srcpath);
  }
  memset(locs, 0, sizeof(*locs) * MAX_STACK);
}

bool unwind_init(struct UnwindState *us) {
  static char *debuginfo_path;
  static const Dwfl_Callbacks proc_callbacks = {
      .find_debuginfo = dwfl_standard_find_debuginfo,
      .debuginfo_path = &debuginfo_path,
      .find_elf = dwfl_linux_proc_find_elf,
  };

  elf_version(EV_CURRENT);
  if (!us->dwfl && !(us->dwfl = dwfl_begin(&proc_callbacks))) {
    LG_WRN("[UNWIND] There was a problem getting the Dwfl");
    return false;
  }

  libdso_init();
  return true;
}

void unwind_free(struct UnwindState *us) {
  FunLoc_clear(us->locs);
  dwfl_end(us->dwfl);
}

int unwindstate__unwind(struct UnwindState *us) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
      .next_thread = next_thread,
      .memory_read = memory_read,
      .set_initial_registers = set_initial_registers,
  };

  LG_DBG("Beginning unwinding for %d:0x%lx-------------------------", us->pid,
         us->eip);
  if (dwfl_linux_proc_report(us->dwfl, us->pid)) {
    LG_WRN("[UNWIND] Could not report module for 0x%lx", us->eip);
    return -1;
  }

  if (dwfl_report_end(us->dwfl, NULL, NULL)) {
    // Not an error, since it means the report context has nothing left to do
    LG_WRN("[UNWIND] dwfl_end was nonzero (%s)", dwfl_errmsg(-1));
    return 0;
  }

  if (!dwfl_attach_state(us->dwfl, NULL, us->pid, &dwfl_callbacks, us)) {
    //    LG_WRN("[UNWIND] dwfl_attach_state was nonzero (%s)",
    //    dwfl_errmsg(-1));
  }

  if (dwfl_getthreads(us->dwfl, tid_cb, us)) {
    LG_WRN("[UNWIND] Could not get thread frames for 0x%lx", us->eip);
    return -1;
  }

  return 0;
}

void analyze_unwinding_error(pid_t pid, uint64_t eip) {
  // expensive operations should not be executed with NDEBUG
#ifndef NDEBUG
  Map *map = procfs_MapMatch(pid, eip);
  // kill 0 to check if the process has finished protected by NDEBUG
  if (!map)
    LG_WRN("Error getting map for [%d](0x%lx)", pid, eip);
  else {
    if (process_is_alive(pid)) {
      LG_WRN("Error unwinding %s [%d](0x%lx)", map->path, pid, eip);
    } else {
      LG_NTC("Process ended before we could unwind [%d]", pid);
    }
  }
#else
  UNUSED(pid);
  UNUSED(eip);
#endif
}

#endif
