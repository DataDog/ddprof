// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "create_elf.hpp"
#include "ddprof_defs.hpp"
#include "ddprof_file_info.hpp"
#include "ddprof_module.hpp"
#include "ddres.hpp"
#include "dso.hpp"
#include "dso_hdr.hpp"

#include <sys/types.h>

extern "C" {
struct Dwfl;
}
namespace ddprof {

struct UnwindState;

struct DwflWrapper {
  explicit DwflWrapper();

  DwflWrapper(DwflWrapper &&other) noexcept { swap(*this, other); }

  DwflWrapper &operator=(DwflWrapper &&other) noexcept {
    swap(*this, other);
    return *this;
  }

  DwflWrapper(const DwflWrapper &other) = delete;            // avoid copy
  DwflWrapper &operator=(const DwflWrapper &other) = delete; // avoid copy

  DDRes attach(pid_t pid, const UniqueElf &ref_elf, bool avoid_new_attach,
               UnwindState *us);

  // unsafe get don't check ranges
  DDProfMod *unsafe_get(FileInfoId_t file_info_id);

  // safe get
  DDRes register_mod(ProcessAddress_t pc, const Dso &dso,
                     const FileInfoValue &fileInfoValue, DDProfMod **mod);

  ~DwflWrapper();

  static void swap(DwflWrapper &first, DwflWrapper &second) noexcept {
    std::swap(first._dwfl, second._dwfl);
    std::swap(first._attached, second._attached);
  }

  Dwfl *_dwfl{nullptr};
  bool _attached{false};
  bool _inconsistent{false};

  // Keep track of the files we added to the dwfl object
  std::unordered_map<FileInfoId_t, DDProfMod> _ddprof_mods;
};

} // namespace ddprof
