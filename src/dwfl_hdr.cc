// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_hdr.hpp"

#include "ddprof_module_lib.hpp"
#include "ddres.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace ddprof {

DwflWrapper::DwflWrapper()
    : _dwfl(nullptr), _attached(false), _inconsistent(false) {
  // for split debug, we can fill the debuginfo_path
  static const Dwfl_Callbacks proc_callbacks = {
      .find_elf = dwfl_linux_proc_find_elf,
      .find_debuginfo = dwfl_standard_find_debuginfo,
      .section_address = nullptr,
      .debuginfo_path = nullptr,
  };

  _dwfl = dwfl_begin(&proc_callbacks);
  if (!_dwfl) {
    LG_WRN("dwfl_begin was zero (%s)", dwfl_errmsg(-1));
    throw DDException(ddres_error(DD_WHAT_DWFL_LIB_ERROR));
  }
} // namespace ddprof

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

DDProfMod *DwflWrapper::unsafe_get(FileInfoId_t file_info_id) {
  auto it = _ddprof_mods.find(file_info_id);
  if (it == _ddprof_mods.end()) {
    return nullptr;
  }
  return &it->second;
}

DDProfMod *DwflWrapper::register_mod(ProcessAddress_t pc, const Dso &dso,
                                     const DDProfModRange &mod_range,
                                     const FileInfoValue &fileInfoValue) {

  DDProfMod new_mod;
  DDRes res = update_module(_dwfl, pc, mod_range, fileInfoValue, new_mod);
  _inconsistent = new_mod._status == DDProfMod::kInconsistent;
  if (IsDDResNotOK(res)) {
    return nullptr;
  }
  if (IsDDResNotOK(update_bias(new_mod))) {
    LG_DBG("Unable to compute bias from file (%s), PID %d, mod[%lx;%lx]",
           fileInfoValue.get_path().c_str(), dso._pid, new_mod._low_addr,
           new_mod._high_addr);
    return nullptr;
  }
  return &(_ddprof_mods[fileInfoValue.get_id()] = new_mod);
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

int DwflHdr::get_nb_mod() const {
  int nb_mods = 0;
  std::for_each(
      _dwfl_map.begin(), _dwfl_map.end(),
      [&](std::unordered_map<pid_t, DwflWrapper>::value_type const &el) {
        nb_mods += el.second._ddprof_mods.size();
      });
  return nb_mods;
}

void DwflHdr::display_stats() const {
  LG_NTC("DWFL_HDR  | %10s | %d", "NB MODS", get_nb_mod());
}

void DwflHdr::clear_pid(pid_t pid) { _dwfl_map.erase(pid); }

} // namespace ddprof
