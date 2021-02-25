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

int debuginfo_get(Dwfl_Module *mod, void **arg, const char *modname, Dwarf_Addr base, const char *file_name, const char*debuglink_file, GElf_Word debuglink_crc, char **debuginfo_file_name) {
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
    mod = dwfl_report_elf(us->dwfl, "WTF?", map->path, -1, map->start - map->off, false);
    D("First check didn't work, the second check is %p:%s", (void*)mod, mod->name);
  }

  if (!mod) {
    // TODO this is where I would put my build-id failover IF I HAD ONE
    D("This is awful, I couldn't report elf.");
    return -1;
  }

  if(mod) {
    Dwfl_Module *mod2 = dwfl_addrmodule(us->dwfl, ip);
    D("mod2: %p", (void*)mod2);
  }

  return mod && dwfl_addrmodule(us->dwfl, ip) == mod ? 0 : -1;
}

int unwindstate__unwind(struct UnwindState *us, struct FunLoc *locs, int max_stack) {
  uint64_t ips[max_stack];
  int n = 0;

  // Initialize ELF
  D("Gonna unwind at %d (my PID is %d)\n", us->pid, getpid());
  elf_version(EV_CURRENT);

  if (!(us->dwfl = dwfl_start())) {
    D("There was a problem getting the Dwfl");
    return -1;
  }

  ips[n++] = us->eip;
  if(report_module(us, *ips)) {
    D("There was a problem reporting the module.");
    return -1;
  }

  return 0;
}

#endif
