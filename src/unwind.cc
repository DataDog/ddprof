// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind.hpp"

#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "logger.hpp"
#include "signal_helper.hpp"
#include "symbol_hdr.hpp"
#include "unwind_dwfl.hpp"
#include "unwind_helpers.hpp"
#include "unwind_metrics.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <array>

using namespace std::string_view_literals;

namespace ddprof {

namespace {
bool is_ld(const std::string &path) {
  // path is expected to not contain slashes
  assert(path.rfind('/') == std::string::npos);

  return path.starts_with("ld-");
}

bool is_stack_complete(UnwindState *us) {
  static constexpr std::array s_expected_root_frames{
      "__clone"sv,
      "__clone3"sv,
      "_exit"sv,
      "main"sv,
      "runtime.goexit.abi0"sv,
      "runtime.systemstack.abi0"sv,
      "_start"sv,
      "start_thread"sv,
      "start_task"sv};

  if (us->output.locs.empty()) {
    return false;
  }

  const auto &root_loc = us->output.locs.back();
  const auto &root_mapping =
      us->symbol_hdr._mapinfo_table[root_loc._map_info_idx];

  // If we are in ld.so (eg. during lib init before main) consider the stack as
  // complete
  if (is_ld(root_mapping._sopath)) {
    return true;
  }

  const auto &root_func =
      us->symbol_hdr._symbol_table[root_loc._symbol_idx]._symname;
  return std::find(s_expected_root_frames.begin(), s_expected_root_frames.end(),
                   root_func) != s_expected_root_frames.end();
}

void find_dso_add_error_frame(DDRes ddres, UnwindState *us) {
  if (ddres._what == DD_WHAT_UW_MAX_PIDS) {
    add_common_frame(us, SymbolErrors::max_pids);
  } else {
    DsoHdr::DsoFindRes const find_res =
        us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
    add_error_frame(find_res.second ? &(find_res.first->second) : nullptr, us,
                    us->current_ip);
  }
}

void add_container_id(UnwindState *us) {
  auto container_id = us->process_hdr.get_container_id(us->pid);
  if (container_id) {
    us->output.container_id = *container_id;
  }
}
} // namespace

void unwind_init() { elf_version(EV_CURRENT); }

void unwind_init_sample(UnwindState *us, const uint64_t *sample_regs,
                        pid_t sample_pid, uint64_t sample_size_stack,
                        const char *sample_data_stack) {
  us->output.clear();
  memcpy(&us->initial_regs.regs[0], sample_regs,
         k_nb_registers_to_unwind * sizeof(uint64_t));
  us->current_ip = us->initial_regs.regs[REGNAME(PC)];
  us->pid = sample_pid;
  us->stack_sz = sample_size_stack;
  us->stack = sample_data_stack;
}

DDRes unwindstate_unwind(UnwindState *us) {
  DDRes res = ddres_init();
  if (us->pid != 0) { // we can not unwind pid 0
    res = unwind_dwfl(us);
  }
  if (IsDDResNotOK(res)) {
    find_dso_add_error_frame(res, us);
  } else if (!is_stack_complete(us)) {
    us->output.is_incomplete = true;
    ddprof_stats_add(STATS_UNWIND_INCOMPLETE_STACK, 1, nullptr);
    // Only add [incomplete] virtual frame if stack is not already truncated !
    if (!is_max_stack_depth_reached(*us)) {
      add_common_frame(us, SymbolErrors::incomplete_stack);
    }
  } else {
    us->output.is_incomplete = false;
  }
  ddprof_stats_add(STATS_UNWIND_AVG_STACK_DEPTH, us->output.locs.size(),
                   nullptr);

  // Add a frame that identifies executable to which these belong
  add_virtual_base_frame(us);
  add_container_id(us);
  return res;
}

void unwind_pid_free(UnwindState *us, pid_t pid,
                     PerfClock::time_point timestamp) {
  if (!(us->dso_hdr.pid_free(pid, timestamp))) {
    LG_DBG("(PID Free)%d -> avoid free of mappings (%ld)", pid,
           timestamp.time_since_epoch().count());
  }
  us->dwfl_hdr.clear_pid(pid);
  us->symbol_hdr.clear(pid);
  us->process_hdr.clear(pid);
}

void unwind_cycle(UnwindState *us) {
  us->symbol_hdr.display_stats();
  us->symbol_hdr.cycle();
  us->dwfl_hdr.display_stats();
  us->dso_hdr.stats().reset();
  unwind_metrics_reset();
}

} // namespace ddprof
