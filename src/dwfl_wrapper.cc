// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_wrapper.hpp"

#include "ddprof_module_lib.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_thread_callbacks.hpp"
#include "logger.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace ddprof {

DwflWrapper::DwflWrapper() {
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

DDRes DwflWrapper::attach(pid_t pid, UnwindState *us) {
  if (_attached) {
    return {};
  }

  static const Dwfl_Thread_Callbacks callbacks = {
      .next_thread = next_thread,
      .get_thread = nullptr,
      .memory_read = memory_read_dwfl,
      .set_initial_registers = set_initial_registers,
      .detach = nullptr,
      .thread_detach = nullptr,
  };
  if (!dwfl_attach_state(_dwfl, us->ref_elf.get(), pid, &callbacks, us)) {
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  _attached = true;
  return {};
}

DDProfMod *DwflWrapper::unsafe_get(FileInfoId_t file_info_id) {
  auto it = _ddprof_mods.find(file_info_id);
  if (it == _ddprof_mods.end()) {
    return nullptr;
  }
  return &it->second;
}

DDRes DwflWrapper::register_mod(ProcessAddress_t pc, const Dso &dso,
                                const FileInfoValue &fileInfoValue,
                                DDProfMod **mod) {

  DDProfMod new_mod;
  DDRes res = report_module(_dwfl, pc, dso, fileInfoValue, new_mod);
  _inconsistent = new_mod._status == DDProfMod::kInconsistent;

  if (IsDDResNotOK(res)) {
    *mod = nullptr;
    return res;
  }
  *mod = &_ddprof_mods.insert_or_assign(fileInfoValue.get_id(), new_mod)
              .first->second;
  return res;
}

} // namespace ddprof
