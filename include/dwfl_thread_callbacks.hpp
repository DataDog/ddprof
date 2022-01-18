#pragma once

extern "C" {
#include "dwfl_internals.h"
}

#include <dwarf.h>

extern "C" {
pid_t next_thread(Dwfl *, void *, void **);
bool set_initial_registers(Dwfl_Thread *, void *);
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg);
}
