extern "C" {
#include "unwind.h"

#include "ddres.h"
#include "libebl.h"
#include "logger.h"
}

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "unwind_dwfl.hpp"
#include "unwind_helpers.hpp"
#include "unwind_symbols.hpp"

DDRes unwind_init(struct UnwindState *us) {
  DDRES_CHECK_FWD(unwind_symbols_hdr_init(&(us->symbols_hdr)));
  try {
    us->dso_hdr = new DsoHdr();
    ddprof::unwind_dwfl_init(us);
  }
  CatchExcept2DDRes();
  elf_version(EV_CURRENT);
  return ddres_init();
}

void unwind_free(struct UnwindState *us) {
  unwind_symbols_hdr_free(us->symbols_hdr);
  delete us->dso_hdr;
  us->dso_hdr = nullptr;
  ddprof::unwind_dwfl_free(us);
  us->symbols_hdr = NULL;
}

DDRes unwindstate__unwind(UnwindState *us) {
  DDRes res = ddres_init();
  if (us->pid != 0) { // we can not unwind pid 0
    res = ddprof::unwind_dwfl(us);
  }
  ddprof::add_virtual_base_frame(us);
  return res;
}

void unwind_pid_free(struct UnwindState *us, pid_t pid) {
  us->dso_hdr->pid_free(pid);
  us->dwfl_hdr->clear_pid(pid);
  us->symbols_hdr->_base_frame_symbol_lookup.erase(pid);
}
