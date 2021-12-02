// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_hdr.hpp"

extern "C" {
#include "logger.h"
}

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace ddprof {
DwflWrapper::DwflWrapper() : _dwfl(nullptr), _attached(false) {
  static const Dwfl_Callbacks proc_callbacks = {
      .find_elf = dwfl_linux_proc_find_elf,
      .find_debuginfo = dwfl_standard_find_debuginfo,
      .section_address = nullptr,
      .debuginfo_path = nullptr, // specify dir to look into
  };
  _dwfl = dwfl_begin(&proc_callbacks);
  if (!_dwfl) {
    LG_WRN("dwfl_begin was zero (%s)", dwfl_errmsg(-1));
    throw DDException(ddres_error(DD_WHAT_DWFL_LIB_ERROR));
  }
}

DwflWrapper::~DwflWrapper() { dwfl_end(_dwfl); }

DDRes DwflWrapper::attach(pid_t pid, const Dwfl_Thread_Callbacks *callbacks,
                          struct UnwindState *us) {
  if (_attached) {
    return ddres_init();
  }
  if (!dwfl_attach_state(_dwfl, NULL, pid, callbacks, us)) {
    LG_DBG("Failed attaching dwfl on pid %d (%s)", pid, dwfl_errmsg(-1));
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  _attached = true;
  return ddres_init();
}

DwflWrapper &DwflHdr::get_or_insert(pid_t pid) {
  _visited_pid.insert(pid);
  auto it = _dwfl_map.find(pid);
  if (it == _dwfl_map.end()) {
    // insert new dwfl for this pid
    auto pair = _dwfl_map.emplace(pid, DwflWrapper());
    assert(pair.second); // expect insertion to be OK
    return pair.first->second;
  }
  return it->second;
}

void DwflHdr::clear_unvisited() {
  std::vector<pid_t> pids_remove;
  for (auto &el : _dwfl_map) {
    if (_visited_pid.find(el.first) == _visited_pid.end()) {
      // clear this element as it was not visited
      pids_remove.push_back(el.first);
    }
  }
  for (pid_t el : pids_remove) {
    _dwfl_map.erase(el);
    LG_NFO("[DWFL] DWFL Map Clearing PID%d", el);
  }

  // clear the list of visited for next cycle
  _visited_pid.clear();
}

void DwflHdr::clear_pid(pid_t pid) { _dwfl_map.erase(pid); }

} // namespace ddprof
