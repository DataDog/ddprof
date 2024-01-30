// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_module_lib.hpp"

#include "build_id.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "unique_fd.hpp"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <unistd.h>

namespace ddprof {

namespace {

DDRes find_elf_segment(int fd, const std::string &filepath,
                       Offset_t file_offset, Segment &segment) {
  Elf *elf = elf_begin(fd, ELF_C_READ_MMAP, nullptr);
  if (elf == nullptr) {
    LG_WRN("Invalid elf %s", filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }
  defer { elf_end(elf); };
  GElf_Ehdr ehdr_mem;
  GElf_Ehdr *ehdr = gelf_getehdr(elf, &ehdr_mem);
  if (ehdr == nullptr) {
    LG_WRN("Invalid elf %s", filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }

  switch (ehdr->e_type) {
  case ET_EXEC:
  case ET_CORE:
  case ET_DYN: {
    size_t phnum;
    if (unlikely(elf_getphdrnum(elf, &phnum) != 0)) {
      LG_WRN("Invalid elf %s", filepath.c_str());
      return ddres_error(DD_WHAT_INVALID_ELF);
    }
    for (size_t i = 0; i < phnum; ++i) {
      GElf_Phdr phdr_mem;
      GElf_Phdr *ph = gelf_getphdr(elf, i, &phdr_mem);
      if (unlikely(ph == nullptr)) {
        LG_WRN("Invalid elf %s", filepath.c_str());
        return ddres_error(DD_WHAT_INVALID_ELF);
      }
      if (ph->p_type == PT_LOAD && file_offset >= ph->p_offset &&
          file_offset < ph->p_offset + ph->p_filesz) {
        segment =
            Segment{ph->p_vaddr, ph->p_offset, elf_flags_to_prot(ph->p_flags)};
        return {};
      }
    }
    break;
  }
  default:
    LG_WRN("Unsupported elf type (%d) %s", ehdr->e_type, filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }

  LG_WRN("No LOAD segment found for offset %lx in %s", file_offset,
         filepath.c_str());
  return ddres_error(DD_WHAT_NO_MATCHING_LOAD_SEGMENT);
}

DDRes compute_elf_bias(int fd, const std::string &filepath, const Dso &dso,
                       ProcessAddress_t pc, Offset_t &bias) {
  // Compute file offset from pc
  Offset_t const file_offset = pc - dso.start() + dso.offset();

  Segment segment;
  auto res = find_elf_segment(fd, filepath, file_offset, segment);
  if (!IsDDResOK(res)) {
    return res;
  }

  bias = dso.start() - dso.offset() - (segment.addr - segment.offset);

  return {};
}
} // namespace

DDRes report_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod) {
  const std::string &filepath = fileInfoValue.get_path();
  const char *module_name = strrchr(filepath.c_str(), '/') + 1;
  if (fileInfoValue.errored()) { // avoid bouncing on errors
    LG_DBG("DSO Previously errored - mod (%s)", module_name);
    return ddres_warn(DD_WHAT_MODULE);
  }

  Dwfl_Module *mod = dwfl_addrmodule(dwfl, pc);

  if (mod) {
    // There should not be a module already loaded at this address
    const char *main_name = nullptr;
    Dwarf_Addr low_addr;
    Dwarf_Addr high_addr;
    dwfl_module_info(mod, nullptr, &low_addr, &high_addr, nullptr, nullptr,
                     &main_name, nullptr);
    LG_NTC("Incoherent modules[PID=%d]: module %s [%lx-%lx] is already "
           "loaded at %lx(%s[ID#%d])",
           dso._pid, main_name, low_addr, high_addr, pc, filepath.c_str(),
           fileInfoValue.get_id());
    ddprof_mod._status = DDProfMod::kInconsistent;
    return ddres_warn(DD_WHAT_MODULE);
  }

  UniqueFd fd_holder{::open(filepath.c_str(), O_RDONLY)};
  if (!fd_holder) {
    LG_WRN("[Mod] Couldn't open fd to module (%s)", filepath.c_str());
    return ddres_warn(DD_WHAT_MODULE);
  }
  LG_DBG("[Mod] Success opening %s, ", filepath.c_str());

  // Load the file at a matching DSO address
  dwfl_errno(); // erase previous error
  Offset_t bias = 0;
  auto res = compute_elf_bias(fd_holder.get(), filepath, dso, pc, bias);
  if (!IsDDResOK(res)) {
    fileInfoValue.set_errored();
    LG_WRN("Couldn't retrieve offsets from %s(%s)", module_name,
           fileInfoValue.get_path().c_str());
    return res;
  }

  ddprof_mod._mod = dwfl_report_elf(dwfl, module_name, filepath.c_str(),
                                    fd_holder.get(), bias, true);

  // Retrieve build id
  const unsigned char *bits = nullptr;
  GElf_Addr vaddr;
  if (int const size = dwfl_module_build_id(ddprof_mod._mod, &bits, &vaddr);
      size > 0) {
    // ensure we called dwfl_module_getelf first (or this can fail)
    // returns the size
    ddprof_mod.set_build_id(BuildIdSpan{bits, static_cast<unsigned>(size)});
  }

  if (!ddprof_mod._mod) {
    // Ideally we would differentiate pid errors from file errors.
    // For perf reasons we will just flag the file as errored
    fileInfoValue.set_errored();
    LG_WRN("Couldn't addrmodule (%s)[0x%lx], MOD:%s (%s)", dwfl_errmsg(-1), pc,
           module_name, fileInfoValue.get_path().c_str());
    return ddres_warn(DD_WHAT_MODULE);
  }
  // dwfl now has ownership of the file descriptor
  (void)fd_holder.release();
  dwfl_module_info(ddprof_mod._mod, nullptr, &ddprof_mod._low_addr,
                   &ddprof_mod._high_addr, nullptr, nullptr, nullptr, nullptr);
  LG_DBG("Loaded mod from file (%s[ID#%d]), (%s) mod[%lx-%lx] bias[%lx], "
         "build-id: %s",
         fileInfoValue.get_path().c_str(), fileInfoValue.get_id(),
         dwfl_errmsg(-1), ddprof_mod._low_addr, ddprof_mod._high_addr, bias,
         ddprof_mod._build_id.c_str());

  ddprof_mod._sym_bias = bias;
  return {};
}

} // namespace ddprof
