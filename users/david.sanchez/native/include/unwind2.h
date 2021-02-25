#ifndef _H_unwind
#define _H_unwind

#include "libdw.h"
#include "libdwfl.h"
#include "libebl.h"
#include <dwarf.h>
#include <stdbool.h>

// TODO select dwfl_internals based on preproc directive
#include "dwfl_internals.h"
#include "procutils.h"

struct UnwindState {
  Dwfl *dwfl;
  pid_t pid;
  char* stack;
  size_t stack_sz;
  union {
    uint64_t regs[3];
    struct {
      uint64_t ebp;
      uint64_t esp;
      uint64_t eip;
    };
  };
  Map *map;
  int max_stack; // ???
  uint64_t *ips;
  uint64_t idx;
};

struct FunLoc {
  uint64_t ip;        // Relative to file, not VMA
  uint64_t map_start; // Start address of mapped region
  uint64_t map_end;   // End
  uint64_t map_off;   // Offset into file
  char *funname;      // name of the function (mangled, possibly)
  char *srcpath;      // name of the source file, if known
  char *sopath;       // name of the file where the symbol is interned (e.g., .so)
  uint32_t line;      // line number in file
  uint32_t disc;      // discriminator
};

#define D(...) \
  fprintf(stderr, "<%s:%d> ", __FUNCTION__, __LINE__); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n")

int debuginfo_get(Dwfl_Module *mod, void **arg, const char *modname,
                  Dwarf_Addr base, const char *file_name,
                  const char*debuglink_file, GElf_Word debuglink_crc,
                  char **debuginfo_file_name) {
(void)mod;
(void)arg;
(void)modname;
(void)base;
(void)file_name;
(void)debuglink_file;
(void)debuglink_crc;
(void)debuginfo_file_name;
  // This is a big TODO
  return -1;
}

Dwfl* dwfl_start() {
  static char *debuginfo_path;
  static const Dwfl_Callbacks proc_callbacks = {
    .find_debuginfo = dwfl_standard_find_debuginfo,  // TODO, update this for containers?
    .debuginfo_path = &debuginfo_path,
    .find_elf = dwfl_linux_proc_find_elf,
  };
  return dwfl_begin(&proc_callbacks);
}

int report_module(struct UnwindState *us, uint64_t ip) {
  Dwfl_Module *mod;
  Map* map = procfs_MapMatch(us->pid, ip);
  if(!map) {
    D("Could not get the map for this process!");
    return -1;
  }

  if ((mod = dwfl_addrmodule(us->dwfl, ip))) {
    D("That worked!");
    Dwarf_Addr s;
    void **userdatap;
    dwfl_module_info(mod, &userdatap, &s, NULL, NULL, NULL, NULL, NULL);
    *userdatap = map;
    if (s != map->start - map->off) {
      D("Something didn't match. %ld != %ld - %ld", s, map->start, map->off);
    }

  }

  if (!mod) {
    char* short_name = strdup(strrchr(map->path, '/')+1);
    mod = dwfl_report_elf(us->dwfl, short_name, map->path, -1, map->start - map->off, false);
    free(short_name);
  }

  if (!mod) {
    // TODO this is where I would put my build-id failover IF I HAD ONE
    D("This is awful, I couldn't report elf.");
    return -1;
  }

  return mod && dwfl_addrmodule(us->dwfl, ip) == mod ? 0 : -1;
}

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  if (*thread_argp != NULL)
    return 0;
  *thread_argp = arg;
  return dwfl_pid(dwfl);
}

bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  struct UnwindState *us = arg;
  Dwarf_Word regs[3] = {0};

  // I only save three lol
  for(int i=0; i<3; i++)
    regs[i] = us->regs[i];

  return dwfl_thread_state_registers(thread, 0, 3, regs);
}

bool memory_read(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg) {
(void)dwfl;
  struct UnwindState *us = arg;

  uint64_t sp_start = us->esp;
  uint64_t sp_end = sp_start + us->stack_sz;

  // Check for overflow, like perf
  if (addr + sizeof(Dwarf_Word) < addr)
    return false;

  if (addr < sp_start || addr + sizeof(Dwarf_Word) > sp_end) {
    // If we're here, we're not in the stack.  We should interpet addr as an
    // address in VM, not as a file offset.  This also rather strongly assumes
    // that `addr` is an address in the instrumented process' space--in other
    // words, it represents a segment that was actually mapped into the
    // given page.  If, for instance, a segment could have been mapped, but
    // wasn't, we fail (does this even make sense?)
    Map* map = procfs_MapMatch(us->pid, addr);

    // I didn't read past dso.c:dso__data_read_offset(), but it doesn't look
    // like perf will do anything to try to correct the address.
    // TODO verify?
    if(!map)
      return false;

    if (-1 == procfs_MapRead(map, result, sizeof(Dwarf_Word), addr))
      return false;
    return true;
  }

  *result = *(Dwarf_Word *)(&us->stack[addr - sp_start]);
  return true;
}

int frame_cb(Dwfl_Frame *state, void *arg) {
  struct UnwindState* us = arg;
  Dwarf_Addr pc;
  bool isactivation;

  if (!dwfl_frame_pc(state, &pc, NULL)) {
    D("%s", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }

  report_module(us, pc);

  if (!dwfl_frame_pc(state, &pc, &isactivation)) {
    D("%s", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }

  if (!isactivation)
    --pc;

  // Store the pc for later
  us->ips[us->idx++] = pc;
  return DWARF_CB_OK;
}

int unwindstate__unwind(struct UnwindState *us, struct FunLoc *locs) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
    .next_thread = next_thread,
    .memory_read = memory_read,
    .set_initial_registers = set_initial_registers,
  };
  int max_stack = us->max_stack;
  uint64_t ips[us->max_stack];
  us->ips = ips;
  memset(us->ips, 0, sizeof(uint64_t)*us->max_stack);

  // Initialize ELF
  D("Gonna unwind at %d (my PID is %d)\n", us->pid, getpid());
  elf_version(EV_CURRENT);

  if (!(us->dwfl = dwfl_start())) {
    D("There was a problem getting the Dwfl");
    return -1;
  }

  us->ips[us->idx++] = us->eip;
  if(report_module(us, *us->ips)) {
    D("There was a problem reporting the module.");
    return -1;
  }

  // TODO, thread-level?
  if (!dwfl_attach_state(us->dwfl, EM_NONE, us->pid, &dwfl_callbacks, us)) {
    D("Could not attach.");
    return -1;
  }

  if (dwfl_getthread_frames(us->dwfl, us->pid, frame_cb, us) && us->max_stack != max_stack) {
    D("Could not get thread frames.");
    return -1;
  }

  printf(" * 0x%lx\n", us->ips[0]);
  for(uint64_t i=1; i<us->idx; i++) {
    printf("   0x%lx\n", us->ips[i]);
  }

  dwfl_end(us->dwfl);
  return 0;
}

#endif
