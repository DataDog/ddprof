// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_defs.h"
#include "dwfl_internals.h"
#include <sys/types.h>
}
#include "ddres.h"

#include "ddprof_file_info.hpp"
#include "dso.hpp"

#include <unordered_map>
#include <unordered_set>

typedef struct UnwindState UnwindState;

namespace ddprof {

struct DwflWrapper {
  explicit DwflWrapper();

  DwflWrapper(DwflWrapper &&other) : _dwfl(nullptr), _attached(false) {
    swap(*this, other);
  }

  DwflWrapper &operator=(DwflWrapper &&other) {
    swap(*this, other);
    return *this;
  }

  DwflWrapper(const DwflWrapper &other) = delete;            // avoid copy
  DwflWrapper &operator=(const DwflWrapper &other) = delete; // avoid copy

  DDRes attach(pid_t pid, const Dwfl_Thread_Callbacks *callbacks,
               UnwindState *us);

  DDRes register_mod(ProcessAddress_t pc, const Dso &dso,
                     const FileInfoValue &fileInfoValue);

  ~DwflWrapper();

  static void swap(DwflWrapper &first, DwflWrapper &second) noexcept {
    std::swap(first._dwfl, second._dwfl);
    std::swap(first._attached, second._attached);
  }

  Dwfl *_dwfl;
  bool _attached;

  // Keep track of the files we added to the dwfl object
  std::unordered_map<FileInfoId_t, bool> _mod_added;
};

class DwflHdr {
public:
  DwflWrapper &get_or_insert(pid_t pid);
  void clear_unvisited();
  void clear_pid(pid_t pid);

  // get number of accessed modules
  int get_nb_mod() const;
  void display_stats() const;

private:
  std::unordered_map<pid_t, DwflWrapper> _dwfl_map;
  std::unordered_set<pid_t> _visited_pid;
};

} // namespace ddprof
