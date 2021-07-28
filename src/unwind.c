#include "unwind.h"

#include "demangle.h"
#include "logger.h"
#include "signal_helper.h"

#define UNUSED(x) (void)(x)

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
      LG_NTC("[UNWIND] Couldn't get read 0x%lx from %d", addr, us->pid);
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

Dwfl_Module *update_mod(Dwfl *dwfl, int pid, uint64_t pc) {
  Dwfl_Module *mod;
  if (!dwfl)
    return NULL;

  // Lookup DSO
  Dso *dso = dso_find(pid, pc);
  if (!dso) {
    pid_backpopulate(pid); // Update and try again
    if ((dso = dso_find(pid, pc))) {
      LG_DBG("[UNWIND] Located DSO (%s)", dso_path(dso));
    } else {
      LG_DBG("[UNWIND] Did not locate DSO");
      return NULL;
    }
  }

  // Now that we've confirmed a separate lookup for the DSO based on procfs
  // and/or perf_event_open() "extra" events, we do two things
  // 1. Check that dwfl has a cache for this PID/pc combination
  // 2. Check that the given cache is accurate to the DSO
  mod = dwfl_addrmodule(dwfl, pc);
  if (mod) {
    Dwarf_Addr vm_addr;
    dwfl_module_info(mod, 0, &vm_addr, 0, 0, 0, 0, 0);
    if (vm_addr != dso->start - dso->pgoff)
      mod = NULL;
  }

  // Try again if either of the criteria above were false
  if (!mod) {
    char *dso_filepath = dso_path(dso);
    if (dso && '[' != *dso_filepath) {
      char *dso_name = strrchr(dso_filepath, '/') + 1;
      mod = dwfl_report_elf(dwfl, dso_name, dso_filepath, -1,
                            dso->start - dso->pgoff, false);
    }
  }
  if (!mod) {
    LG_WRN(
        "[UNWIND] couldn't addrmodule (%s)[0x%lx], but got DSO %s[0x%lx:0x%lx]",
        dwfl_errmsg(-1), pc, dso_path(dso), dso->start, dso->end);
    return NULL;
  }

  return mod;
}

int frame_cb(Dwfl_Frame *state, void *arg) {
  struct UnwindState *us = arg;
  Dwarf_Addr pc = 0;
  bool isactivation = false;

  // Query the frame state to get the PC.  We skip the expensive check for
  // activation frame because the underlying DSO (module) may not have been
  // cached yet (but we need the PC to generate/check such a cache
  if (!dwfl_frame_pc(state, &pc, NULL)) {
    LG_WRN("[UNWIND] dwfl_frame_pc NULL (%s)", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }

  Dwfl_Module *mod = update_mod(us->dwfl, us->pid, pc);
  if (!mod) {
    LG_WRN("[UNWIND] Unable to retrieve the Dwfl_Module");
    return DWARF_CB_ABORT;
  }

  if (!dwfl_frame_pc(state, &pc, &isactivation)) {
    LG_WRN("[UNWIND] %s", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }
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

  // Now we register
  GElf_Off offset = {0};

  dwflmod_cache_status cache_status = dwfl_module_cache_getinfo(
      us->cache_hdr, mod, pc, us->pid, &offset, &us->locs[us->idx].funname,
      &us->locs[us->idx].line, &us->locs[us->idx].srcpath);
  if (cache_status != K_DWFLMOD_CACHE_OK) {
    LG_ERR("Error from dwflmod_cache_status");
    return DWARF_CB_ABORT;
  }

  us->locs[us->idx].ip = pc;
  us->locs[us->idx].map_start = mod->low_addr;
  us->locs[us->idx].map_end = mod->high_addr;
  us->locs[us->idx].map_off = offset;

  cache_status = dwfl_module_cache_getsname(us->cache_hdr, mod,
                                            &(us->locs[us->idx].sopath));
  if (cache_status != K_DWFLMOD_CACHE_OK) {
    LG_ERR("Error from dwfl_module_cache_getsname");
    return DWARF_CB_ABORT;
  }

  us->idx++;
  return DWARF_CB_OK;
}

int tid_cb(Dwfl_Thread *thread, void *targ) {
  dwfl_thread_getframes(thread, frame_cb, targ);
  return DWARF_CB_OK;
}

void FunLoc_clear(FunLoc *locs) { memset(locs, 0, sizeof(*locs) * MAX_STACK); }

static bool unwind_dwfl_begin(struct UnwindState *us) {
  static char *debuginfo_path;

  static const Dwfl_Callbacks proc_callbacks = {
      .find_debuginfo = dwfl_standard_find_debuginfo,
      .debuginfo_path = &debuginfo_path,
      .find_elf = dwfl_linux_proc_find_elf,
  };
  us->dwfl = dwfl_begin(&proc_callbacks);
  if (!us->dwfl) {
    LG_WRN("[UNWIND] dwfl_begin was zero (%s)", dwfl_errmsg(-1));
    return false;
  }
  us->attached = false;

  return true;
}

static void unwind_dwfl_end(struct UnwindState *us) {
  if (us->dwfl) {
    dwfl_end(us->dwfl);
    us->dwfl = 0;
  }
  us->attached = false;
}

bool dwfl_caches_clear(struct UnwindState *us) {
  dwflmod_cache_status cache_status = dwflmod_cache_hdr_clear(us->cache_hdr);
  if (cache_status != K_DWFLMOD_CACHE_OK) {
    LG_WRN("[UNWIND] Unable to clear intermediate unwinding cache");
    return false;
  }
  unwind_dwfl_end(us);
  return unwind_dwfl_begin(us);
}

static bool unwind_attach(struct UnwindState *us) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
      .next_thread = next_thread,
      .memory_read = memory_read,
      .set_initial_registers = set_initial_registers,
  };

  if (us->attached) {
    return true;
  }
  if (dwfl_attach_state(us->dwfl, NULL, us->pid, &dwfl_callbacks, us)) {
    LG_WRN("[UNWIND] dwfl_attach_state was nonzero (%s)", dwfl_errmsg(-1));
    return false;
  }
  us->attached = true;
  return true;
}

bool unwind_init(struct UnwindState *us) {
  if (dwflmod_cache_hdr_init(&(us->cache_hdr)) != K_DWFLMOD_CACHE_OK) {
    us->cache_hdr = NULL;
    return false;
  }

  elf_version(EV_CURRENT);
  if (!unwind_dwfl_begin(us))
    return false;
  libdso_init();
  return true;
}

void unwind_free(struct UnwindState *us) {
  dwflmod_cache_hdr_free(us->cache_hdr);
  unwind_dwfl_end(us);
  us->cache_hdr = NULL;
}

int unwindstate__unwind(struct UnwindState *us) {
  // Update modules at the top
  update_mod(us->dwfl, us->pid, us->eip);

  if (!unwind_attach(us)) {
    return -1;
  }

  if (!dwfl_getthread_frames(us->dwfl, us->pid, frame_cb, us)) {
    LG_DBG("[UNWIND] dwfl_getthread_frames was nonzero (%s)", dwfl_errmsg(-1));
    return us->idx > 0 ? 0 : -1;
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
