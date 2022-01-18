// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_module.hpp"

extern "C" {
#include "logger.h"
}

namespace ddprof {
Dwfl_Module *update_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                           const FileInfoValue &fileInfoValue) {
  if (!dwfl)
    return nullptr;

  if (fileInfoValue._errored) {
    LG_DBG("DSO Previously errored - mod (%s)", dso._filename.c_str());
    return nullptr;
  }
  // Now that we've confirmed a separate lookup for the DSO based on procfs
  // and/or perf_event_open() "extra" events, we do two things
  // 1. Check that dwfl has a cache for this PID/pc combination
  // 2. Check that the given cache is accurate to the DSO

  Dwfl_Module *mod = dwfl_addrmodule(dwfl, pc);
  if (mod) {
    Dwarf_Addr vm_addr;
    dwfl_module_info(mod, 0, &vm_addr, 0, 0, 0, 0, 0);
    if (vm_addr != dso._start - dso._pgoff) {
      // not using only addresses (as we get off by 1 page errors)
      const std::string &filepath = fileInfoValue.get_path();
      if (filepath.empty()) {
        mod = nullptr;
      } else {
        const char *dso_name = strrchr(filepath.c_str(), '/') + 1;
        if (mod && strcmp(dso_name, mod->name) != 0) {
          LG_NTC("Incoherent DSO (%s) %lx != %lx dwfl_module (%s)", dso_name,
                 dso._start - dso._pgoff, vm_addr, mod->name);
          mod = nullptr;
        }
      }
    }
  }

  // Try again if either if we failed to hit the dwfl cache above
  if (!mod && dso._type == ddprof::dso::kStandard) {
    // assumption is that this string is built only once
    const std::string &filepath = fileInfoValue.get_path();
    if (!filepath.empty()) {
      const char *dso_name = strrchr(filepath.c_str(), '/') + 1;
      dwfl_errno(); // erase previous error
      mod = dwfl_report_elf(dwfl, dso_name, filepath.c_str(), -1,
                            dso._start - dso._pgoff, false);
      LG_DBG("Loaded mod from file (%s), PID %d (%s)[offset:%lx], mod[%lx;%lx]",
             filepath.c_str(), dso._pid, dwfl_errmsg(-1), dso._pgoff,
             mod ? mod->low_addr : 0, mod ? mod->high_addr : 0);
    }
  }
  if (!mod) {
    fileInfoValue._errored = true;
    LG_WRN("couldn't addrmodule (%s)[0x%lx], DSO:%s (%s)", dwfl_errmsg(-1), pc,
           dso.to_string().c_str(), fileInfoValue.get_path().c_str());
    return mod;
  }

  return mod;
}

} // namespace ddprof
