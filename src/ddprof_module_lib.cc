// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_module_lib.hpp"

#include "ddres.hpp"
#include "logger.hpp"

namespace ddprof {

DDRes update_module(Dwfl *dwfl, ProcessAddress_t pc,
                    const DDProfModRange &mod_range,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod) {
  const std::string &filepath = fileInfoValue.get_path();
  const char *module_name = strrchr(filepath.c_str(), '/') + 1;
  if (fileInfoValue._errored) { // avoid bouncing on errors
    LG_DBG("DSO Previously errored - mod (%s)", module_name);
    return ddres_warn(DD_WHAT_MODULE);
  }

  // Now that we've confirmed a separate lookup for the DSO based on procfs
  // and/or perf_event_open() "extra" events, we do two things
  // 1. Check that dwfl has a cache for this PID/pc combination
  // 2. Check that the given cache is accurate to the DSO
  ddprof_mod._mod = dwfl_addrmodule(dwfl, pc);

  if (ddprof_mod._mod) {
    const char *main_name = nullptr;
    dwfl_module_info(ddprof_mod._mod, 0, &ddprof_mod._low_addr,
                     &ddprof_mod._high_addr, 0, 0, &main_name, 0);
    if (ddprof_mod._low_addr != mod_range._low_addr) {
      LG_NTC("Incoherent Modules (%s-%s) %lx != %lx dwfl_module)", module_name,
             main_name, mod_range._low_addr, ddprof_mod._low_addr);
      ddprof_mod._status = DDProfMod::kInconsistent;
      return ddres_warn(DD_WHAT_MODULE);
    }
    return ddres_init();
  }

  // Load the file at a matching DSO address
  if (!ddprof_mod._mod && fileInfoValue.get_id() > k_file_info_error) {
    if (!filepath.empty()) {
      dwfl_errno(); // erase previous error
      ddprof_mod._mod = dwfl_report_elf(dwfl, module_name, filepath.c_str(), -1,
                                        mod_range._low_addr, false);
    }
  }

  if (!ddprof_mod._mod) {
    // Ideally we would differentiate pid errors from file errors.
    // For perf reasons we will just flag the file as errored
    fileInfoValue._errored = true;
    LG_WRN("Couldn't addrmodule (%s)[0x%lx], MOD:%s (%s)", dwfl_errmsg(-1), pc,
           module_name, fileInfoValue.get_path().c_str());
    return ddres_warn(DD_WHAT_MODULE);
  } else {
    dwfl_module_info(ddprof_mod._mod, 0, &ddprof_mod._low_addr,
                     &ddprof_mod._high_addr, 0, 0, 0, 0);
    LG_DBG("Loaded mod from file (%s), (%s) mod[%lx;%lx]",
           fileInfoValue.get_path().c_str(), dwfl_errmsg(-1),
           ddprof_mod._low_addr, ddprof_mod._high_addr);
  }
  return ddres_init();
}

DDRes update_bias(DDProfMod &ddprof_mod) {
  // Retrieve the biais to figure out the offset
  // We can use the dwarf CFI (dwfl_module_eh_cfi)
  // or the ELF (dwfl_module_getelf).
  // Considering dwarf is not always available, prefer elf
  Elf *elf = dwfl_module_getelf(ddprof_mod._mod, &ddprof_mod._sym_bias);
  if (!elf) {
    return ddres_warn(DD_WHAT_MODULE);
  }
  return ddres_init();
}

} // namespace ddprof
