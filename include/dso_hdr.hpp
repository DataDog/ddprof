// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// C++ flat set (less allocs than std::set)
#include <amc/flatset.hpp>
#include <array>
#include <cassert>
#include <string>
#include <unordered_map>

#include "dso.hpp"

namespace ddprof {

#define DSO_EVENT_TABLE(XX)                                                    \
  XX(kUnhandledDso, "Unhandled")                                               \
  XX(kUnwindFailure, "Failure")                                                \
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

  void incr_metric(DsoEventType dso_event, dso::DsoType path_type) {
    assert(dso_event < kNbDsoEventTypes);
    ++_metrics[dso_event][path_type];
  }

  uint64_t sum_event_metric(DsoEventType dso_event) const;

  void log() const;
  void reset() {
    for (auto &metric_array : _metrics)
      reset_event_metric(metric_array);
  }

private:
  static const char *s_event_dbg_str[kNbDsoEventTypes];
  static void
  reset_event_metric(std::array<uint64_t, dso::kNbDsoTypes> &metric_array) {
    std::fill(metric_array.begin(), metric_array.end(), 0);
  }
  // log events according to dso types
  std::array<std::array<uint64_t, dso::kNbDsoTypes>, kNbDsoEventTypes> _metrics;
};

/**************
 * DSO Header *
 **************/
/// Keep track of binaries and associate them to address ranges
class DsoHdr {
public:
  /******* Structures and types **********/
  typedef amc::FlatSet<Dso> DsoSet;
  typedef std::unordered_map<pid_t, DsoSet> DsoPidMap;

  typedef DsoSet::const_iterator DsoSetConstIt;
  typedef DsoSet::iterator DsoSetIt;

  /* Range is assumed as [start, end) */
  typedef std::pair<DsoSetIt, DsoSetIt> DsoRange;
  typedef std::pair<DsoSetConstIt, bool> DsoFindRes;

  /* Path and inode information */
  typedef std::pair<std::string, inode_t> FileLocInfo_t;

  /******* MAIN APIS **********/
  DsoHdr();

  // Add the element check for overlap and remove them
  DsoFindRes insert_erase_overlap(Dso &&dso);
  DsoFindRes insert_erase_overlap(DsoSet &set, Dso &&dso);

  DsoFindRes pid_read_dso(int pid, void *buf, size_t sz, uint64_t addr);

  // Clear all dsos and regions associated with this pid
  void pid_free(int pid);

  // Find the first associated to this pid
  DsoFindRes dso_find_first_std_executable(pid_t pid);

  // Find the closest dso to this pid and addr
  DsoFindRes dso_find_closest(pid_t pid, ElfAddress_t addr);

  static DsoFindRes dso_find_closest(const DsoSet &set, pid_t pid,
                                     ElfAddress_t addr);

  bool dso_handled_type_read_dso(const Dso &dso);

  DsoFindRes dso_find_or_backpopulate(pid_t pid, ElfAddress_t addr);

  void reset_backpopulate_state() { _backpopulate_state_map.clear(); }
  /******* HELPERS **********/
  // Find the dso if same
  static DsoFindRes dso_find_same_or_smaller(const DsoSet &set, const Dso &dso);

  // Returns a range that points on _set.end() if nothing was found
  static DsoRange get_intersection(const DsoSet &set, const Dso &dso);

  // Helper to create a dso from a line in /proc/pid/maps
  static Dso dso_from_procline(int pid, char *line);

  static DsoFindRes find_res_not_found(const DsoSet &set) {
    return std::make_pair<DsoSetConstIt, bool>(set.end(), false);
  }

  DsoFindRes find_res_not_found(int pid) {
    return std::make_pair<DsoSetConstIt, bool>(_map[pid].end(), false);
  }

  // returns an empty string if it can't find the binary
  FileLocInfo_t get_binary_loc_info(const Dso &dso);

  int get_nb_dso() const;
  int get_nb_mapped_dso() const;
  /********* Region helpers ***********/
  // returns null if the file was not found
  const RegionHolder *find_or_insert_region(const Dso &dso);

  // Unordered map of sorted
  DsoPidMap _map;
  struct DsoStats _stats;

private:
  enum BackpopulatePermission {
    kForbidden,
    kAllowed,
  };

  struct BackpopulateState {
    BackpopulateState() : _nbUnfoundDsos(), _perm(kAllowed) {}
    int _nbUnfoundDsos;
    BackpopulatePermission _perm;
  };

  // Associate pid to a backpopulation state
  typedef std::unordered_map<pid_t, BackpopulateState> BackpopulateStateMap;

  // parse procfs to look for dso elements
  bool pid_backpopulate(DsoSet &set, pid_t pid, int &nb_elts_added);

  // erase range of elements
  static void erase_range(DsoSet &set, const DsoRange &range);

  DsoUID_t find_or_add_dso_uid(const Dso &dso);

  BackpopulateStateMap _backpopulate_state_map;
  RegionMap _region_map;
  std::unordered_map<RegionKey, DsoUID_t> _dso_uid_map;
  // Associate unique IDs even for different PIDs
  DsoUID_t _next_dso_id;
  // /proc files can be mounted at various places (whole host profiling)
  std::string _path_to_proc;
};

} // namespace ddprof
