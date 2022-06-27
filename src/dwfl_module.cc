// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_module.hpp"

#include "logger.hpp"

namespace ddprof {

DDProfMod update_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                        DDProfModRange mod_range,
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

  if (ddprof_mod._mod) {
    const char *main_name = nullptr;
    dwfl_module_info(ddprof_mod._mod, 0, &ddprof_mod._low_addr,
                     &ddprof_mod._high_addr, 0, 0, &main_name, 0);
    if (ddprof_mod._low_addr != mod_range._low_addr) {
      LG_NTC("Incoherent DSO (%s) %lx != %lx dwfl_module)",
             dso._filename.c_str(), mod_range._low_addr, ddprof_mod._low_addr);
      return DDProfMod(DDProfMod::kInconsistent);
    }
    return ddprof_mod;
  }

  // Load the file at a matching DSO address
  if (!ddprof_mod._mod && dso._type == ddprof::dso::kStandard) {
    const std::string &filepath = fileInfoValue.get_path();
    if (!filepath.empty()) {
      const char *dso_name = strrchr(filepath.c_str(), '/') + 1;
      dwfl_errno(); // erase previous error
      ddprof_mod._mod = dwfl_report_elf(dwfl, dso_name, filepath.c_str(), -1,
                                        mod_range._low_addr, false);
    }
  }

  if (!ddprof_mod._mod) {
    // Ideally we would differentiate pid errors from file errors.
    // For perf reasons we will just flag the file as errored
    fileInfoValue._errored = true;
    LG_WRN("Couldn't addrmodule (%s)[0x%lx], DSO:%s (%s)", dwfl_errmsg(-1), pc,
           dso.to_string().c_str(), fileInfoValue.get_path().c_str());
  } else {
    dwfl_module_info(ddprof_mod._mod, 0, &ddprof_mod._low_addr,
                     &ddprof_mod._high_addr, 0, 0, 0, 0);
    LG_DBG("Loaded mod from file (%s), PID %d (%s) mod[%lx;%lx]",
           fileInfoValue.get_path().c_str(), dso._pid, dwfl_errmsg(-1),
           ddprof_mod._low_addr, ddprof_mod._high_addr);
  }
  // TODO: Figure out how to check that mapping makes sense
  // ddprof_mod._high_addr != mod_range._high_addr
  return ddprof_mod;
}

} // namespace ddprof
