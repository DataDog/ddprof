// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <array>
#include <cassert>
#include <map>
#include <string>
#include <unordered_map>

#include "ddprof_file_info.hpp"
#include "ddprof_module.hpp"
#include "ddres_def.hpp"
#include "dso.hpp"
#include "perf_clock.hpp"

namespace ddprof {

#define DSO_EVENT_TABLE(XX)                                                    \
  XX(kTargetDso, "Target")                                                     \
  XX(kNewDso, "New")

#define X_DSO_EVENT_ENUM(a, b) a,
#define X_DSO_EVENT_DBG_STR(a, b) b,

class DsoStats {
public:
  DsoStats() : _metrics{} {}

  enum DsoEventType {
    DSO_EVENT_TABLE(X_DSO_EVENT_ENUM) kNbDsoEventTypes,
  };

  void incr_metric(DsoEventType dso_event, DsoType path_type) {
    assert(dso_event < kNbDsoEventTypes);
    ++_metrics[dso_event][static_cast<size_t>(path_type)];
  }

  [[nodiscard]] uint64_t sum_event_metric(DsoEventType dso_event) const;

  void log() const;
  void reset() {
    for (auto &metric_array : _metrics) {
      reset_event_metric(metric_array);
    }
  }

private:
  using MetricPerDsoType =
      std::array<uint64_t, static_cast<size_t>(DsoType::kNbDsoTypes)>;
  static const char *s_event_dbg_str[kNbDsoEventTypes];
  static void reset_event_metric(MetricPerDsoType &metric_array) {
    std::fill(metric_array.begin(), metric_array.end(), 0);
  }
  // log events according to dso types
  std::array<MetricPerDsoType, kNbDsoEventTypes> _metrics;
};

/**************
 * DSO Header *
 **************/
/// Keep track of binaries and associate them to address ranges
/// We have 3 levels of information per DSO
///
/// PID map : split everything per PID
/// Map of DSOs : information from proc map (addresses / binary name)
/// File info : latest location of the file and unique ID to represent it
class DsoHdr {
public:
  /******* Structures and types **********/
  using DsoMap = std::map<ProcessAddress_t, Dso>;

  enum BackpopulatePermission {
    kForbidden,
    kAllowed,
  };

  struct BackpopulateState {
    static constexpr int k_nb_requests_between_backpopulates = 10;
    int nb_unfound_dsos{};
    PerfClock::time_point last_backpopulate_time{};
    BackpopulatePermission perm{kAllowed};
  };

  struct PidMapping {
    DsoMap _map;
    BackpopulateState _backpopulate_state;
    // save the start addr of the jit dump info if available
    ProcessAddress_t _jitdump_addr = {};
  };
  using DsoPidMap = std::unordered_map<pid_t, PidMapping>;

  using DsoMapConstIt = DsoMap::const_iterator;
  using DsoMapIt = DsoMap::iterator;

  /* Range is assumed as [start, end) */
  using DsoRange = std::pair<DsoMapIt, DsoMapIt>;
  using DsoConstRange = std::pair<DsoMapConstIt, DsoMapConstIt>;
  using DsoFindRes = std::pair<DsoMapConstIt, bool>;

  /******* MAIN APIS **********/
  explicit DsoHdr(std::string_view path_to_proc = "", int dd_profiling_fd = -1);

  // Add the element check for overlap and remove them
  DsoFindRes insert_erase_overlap(Dso &&dso);
  DsoFindRes insert_erase_overlap(PidMapping &pid_mapping, Dso &&dso);

  /// @brief Insert DSO if timestamp is posterior to latest backpopulate
  /// @param dso Dso to insert
  /// @param timestamp Timestamp of mmap event
  /// @return false if Dso is discarded because timestamp is too old, true
  ///         otherwise
  bool maybe_insert_erase_overlap(Dso &&dso, PerfClock::time_point timestamp);

  // Clear all dsos and regions associated with this pid
  void pid_free(int pid);

  // Duplicate mapping info from parent_pid into pid
  void pid_fork(pid_t pid, pid_t parent_pid);

  // Find the first associated to this pid
  bool find_exe_name(pid_t pid, std::string &exe_name);
  DsoFindRes dso_find_first_std_executable(pid_t pid);

  // Find the closest dso to this pid and addr
  DsoFindRes dso_find_closest(pid_t pid, ElfAddress_t addr);

  static DsoFindRes dso_find_closest(const DsoMap &map, ElfAddress_t addr);

  // parse procfs to look for dso elements
  bool pid_backpopulate(pid_t pid, int &nb_elts_added);

  // find or parse procfs if allowed
  DsoFindRes dso_find_or_backpopulate(PidMapping &pid_mapping, pid_t pid,
                                      ElfAddress_t addr);
  DsoFindRes dso_find_or_backpopulate(pid_t pid, ElfAddress_t addr);

  void reset_backpopulate_state(
      int reset_threshold =
          BackpopulateState::k_nb_requests_between_backpopulates);
  /******* HELPERS **********/
  // Find the dso if same
  static DsoFindRes dso_find_adjust_same(DsoMap &map, const Dso &dso);

  // Returns an empty range if nothing was found
  DsoRange get_intersection(pid_t pid, const Dso &dso);

  // Returns an empty range if nothing was found
  static DsoRange get_intersection(DsoMap &map, const Dso &dso);

  // Return whole mapping range associated with the same elf file
  static DsoConstRange get_elf_range(const DsoMap &map, DsoMapConstIt it);

  // Helper to create a dso from a line in /proc/pid/maps
  static Dso dso_from_proc_line(int pid, const char *line);

  static DsoFindRes find_res_not_found(const DsoMap &map) {
    return {map.end(), false};
  }

  DsoFindRes find_res_not_found(int pid) {
    // not const as it can create an element if the map does not exist for pid
    return {_pid_map[pid]._map.end(), false};
  }

  // Access file and retrieve absolute path and ID
  FileInfoId_t get_or_insert_file_info(const Dso &dso);

  // returns an empty string if it can't find the binary
  FileInfo find_file_info(const Dso &dso);

  const FileInfoValue &get_file_info_value(FileInfoId_t id) const {
    return _file_info_vector[id];
  }

  void set_path_to_proc(std::string_view path_to_proc) {
    _path_to_proc = path_to_proc;
  }
  const std::string &get_path_to_proc() const { return _path_to_proc; }

  int get_nb_dso() const;

  const DsoStats &stats() const { return _stats; }
  DsoStats &stats() { return _stats; }

  PidMapping &get_pid_mapping(pid_t pid) { return _pid_map[pid]; }

  bool check_invariants() const;

private:
  // erase range of elements
  static void erase_range(DsoMap &map, DsoRange range, const Dso &new_mapping);

  // parse procfs to look for dso elements
  bool pid_backpopulate(PidMapping &pid_mapping, pid_t pid, int &nb_elts_added);

  FileInfoId_t update_id_from_dso(const Dso &dso);

  FileInfoId_t update_id_dd_profiling(const Dso &dso);

  FileInfoId_t update_id_from_path(const Dso &dso);

  // Unordered map (by pid) of sorted DSOs
  DsoPidMap _pid_map;
  DsoStats _stats;
  FileInfoInodeMap _file_info_inode_map;
  FileInfoVector _file_info_vector;
  std::string _path_to_proc; // /proc files can be mounted at various places
                             // (whole host profiling)
  int _dd_profiling_fd;
  // Assumption is that we have a single version of the dd_profiling library
  // across all PIDs.
  FileInfoId_t _dd_profiling_file_info = k_file_info_undef;
};

} // namespace ddprof
