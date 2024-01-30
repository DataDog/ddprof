// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddprof_file_info.hpp"
#include "ddprof_module.hpp"
#include "ddres.hpp"
#include "dso.hpp"
#include "dso_hdr.hpp"
#include "dwfl_internals.hpp"

#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>

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

  DDRes attach(pid_t pid, const Dwfl_Thread_Callbacks *callbacks,
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

class DwflHdr {
public:
  // checks against maximum number of PIDs
  DwflWrapper *get_or_insert(pid_t pid);
  std::vector<pid_t> get_unvisited() const;
  void reset_unvisited();
  void clear_pid(pid_t pid);

  // get number of accessed modules
  int get_nb_mod() const;
  void display_stats() const;

private:
  std::unordered_map<pid_t, DwflWrapper> _dwfl_map;
  std::unordered_set<pid_t> _visited_pid;
};

} // namespace ddprof
