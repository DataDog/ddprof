// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_module.hpp"

extern "C" {
#include "logger.h"
}

namespace ddprof {
DDProfMod update_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                        const FileInfoValue &fileInfoValue) {
  if (!dwfl)
    return DDProfMod();

  if (fileInfoValue._errored) {
    LG_DBG("DSO Previously errored - mod (%s)", dso._filename.c_str());
    return DDProfMod();
  }
  // Now that we've confirmed a separate lookup for the DSO based on procfs
  // and/or perf_event_open() "extra" events, we do two things
  // 1. Check that dwfl has a cache for this PID/pc combination
  // 2. Check that the given cache is accurate to the DSO

  DDProfMod ddprof_mod;

  ddprof_mod._mod = dwfl_addrmodule(dwfl, pc);
  Dwarf_Addr low_addr = 0;
  Dwarf_Addr high_addr = 0;

  if (ddprof_mod._mod) {
    const char *main_name = nullptr;
    dwfl_module_info(ddprof_mod._mod, 0, &ddprof_mod._low_addr,
                     &ddprof_mod._high_addr, 0, 0, &main_name, 0);
    LG_WRN("Retrieveing mod with file : %s vs main.name %s",
           fileInfoValue.get_path().c_str(), main_name);
    if (ddprof_mod._low_addr != dso._start - dso._pgoff) {
      // not using only addresses (as we get off by 1 page errors)
      const std::string &filepath = fileInfoValue.get_path();
      if (filepath.empty() || main_name == nullptr) {
        ddprof_mod._mod = nullptr;
      } else {
        // A dso replaced the mod - todo free this dwfl object
        // Although the check is slightly fragile
        // we are not comparing the full path as we use the proc maps
        // to access them. So the root path could change while
        // this would still be the same file.
        const char *dso_name = strrchr(filepath.c_str(), '/') + 1;
        const char *mod_name = strrchr(main_name, '/') + 1;
        if (strcmp(mod_name, dso_name) != 0) {
          LG_NTC("Incoherent DSO (%s) %lx != %lx dwfl_module (%s)",
                 filepath.c_str(), dso._start - dso._pgoff, low_addr,
                 main_name);
          ddprof_mod._mod = nullptr;
        }
      }
    }
  }

  // Try again if either if we failed to hit the dwfl cache above
  if (!ddprof_mod._mod && dso._type == ddprof::dso::kStandard) {
    // assumption is that this string is built only once
    const std::string &filepath = fileInfoValue.get_path();
    if (!filepath.empty()) {
      const char *dso_name = strrchr(filepath.c_str(), '/') + 1;
      dwfl_errno(); // erase previous error
      ddprof_mod._mod = dwfl_report_elf(dwfl, dso_name, filepath.c_str(), -1,
                                        dso._start - dso._pgoff, false);
      LG_DBG("Loaded mod from file (%s), PID %d (%s)[offset:%lx], mod[%lx;%lx]",
             filepath.c_str(), dso._pid, dwfl_errmsg(-1), dso._pgoff,
             ddprof_mod._low_addr, ddprof_mod._high_addr);
    }
  }
  if (!ddprof_mod._mod) {
    fileInfoValue._errored = true;
    LG_WRN("couldn't addrmodule (%s)[0x%lx], DSO:%s (%s)", dwfl_errmsg(-1), pc,
           dso.to_string().c_str(), fileInfoValue.get_path().c_str());
    return ddprof_mod;
  }

  return ddprof_mod;
}

} // namespace ddprof
