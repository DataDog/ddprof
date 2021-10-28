#pragma once

extern "C" {
#include "ddprof_defs.h"
#include "ddres_def.h"
#include "unwind_state.h"
}
#include "dso_hdr.hpp"

typedef struct Dwfl Dwfl;
typedef struct Dwfl_Module Dwfl_Module;

namespace ddprof {
// Structure to group Dso and Mod
struct DsoMod {
  explicit DsoMod(DsoFindRes find_res)
      : _dso_find_res(find_res), _dwfl_mod(nullptr) {}
  DsoFindRes _dso_find_res;
  Dwfl_Module *_dwfl_mod;
};

DsoMod update_mod(DsoHdr *dso_hdr, Dwfl *dwfl, int pid, ProcessAddress_t pc);

SymbolIdx_t add_dwfl_frame(UnwindState *us, DsoMod dso_mod, ElfAddress_t pc);

} // namespace ddprof
