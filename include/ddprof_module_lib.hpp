// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_file_info.hpp"
#include "ddprof_module.hpp"
#include "ddres_def.hpp"
#include "dso.hpp"
#include "dso_hdr.hpp"
#include "dwfl_internals.hpp"

namespace ddprof {

// Convert elf flags to mmap prot flags
inline uint32_t elf_flags_to_prot(Elf64_Word flags) {
  return ((flags & PF_X) ? PROT_EXEC : 0) | ((flags & PF_R) ? PROT_READ : 0) |
      ((flags & PF_W) ? PROT_WRITE : 0);
}

// Structure to represent both a mapping and a loadable elf segment
struct Segment {
  ElfAddress_t addr;
  Offset_t offset;
  uint32_t prot;
};

// From a dso object (and the matching file), attach the module to the dwfl
// object, return the associated Dwfl_Module
DDRes report_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod);

} // namespace ddprof
