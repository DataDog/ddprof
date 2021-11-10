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

#include "dso.hpp"

namespace ddprof {
typedef amc::FlatSet<Dso> DsoSet;
typedef DsoSet::const_iterator DsoSetConstIt;
typedef DsoSet::iterator DsoSetIt;

/* Range is assumed as [start, end) */
typedef std::pair<DsoSetIt, DsoSetIt> DsoRange;

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

} // namespace ddprof

// Out of namespace to be linked from C code
typedef std::pair<ddprof::DsoSetConstIt, bool> DsoFindRes;

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

struct DsoHdr {
  DsoHdr() : _next_dso_id(0) {}

  /******* MAIN APIS **********/
  // Add the element check for overlap and remove them
  DsoFindRes insert_erase_overlap(ddprof::Dso &&dso);

  DsoFindRes pid_read_dso(int pid, void *buf, size_t sz, uint64_t addr);

  // Clear all dsos and regions associated with this pid
  void pid_free(int pid);

  // parse procfs to look for dso elements
  bool pid_backpopulate(int, int &nb_elts_added);

  // Find the first associated to this pid
  DsoFindRes dso_find_first_std_executable(pid_t pid) const;

  // Find the closest dso to this pid and addr
  DsoFindRes dso_find_closest(pid_t pid, ElfAddress_t addr);

  bool dso_handled_type_read_dso(const ddprof::Dso &dso);

  DsoFindRes dso_find_or_backpopulate(pid_t pid, ElfAddress_t addr);

  void reset_backpopulate_state() { _backpopulate_state_map.clear(); }
  /******* HELPERS **********/
  // Find the dso if same
  DsoFindRes dso_find_same_or_smaller(const ddprof::Dso &dso);

  // Returns a range that points on _set.end() if nothing was found
  ddprof::DsoRange get_intersection(const ddprof::Dso &dso);

  // get all elements of a pid
  ddprof::DsoRange get_pid_range(pid_t pid);

  // erase range of elements
  void erase_range(const ddprof::DsoRange &range);

  // Helper to create a dso from a line in /proc/pid/maps
  static ddprof::Dso dso_from_procline(int pid, char *line);

  DsoFindRes find_res_not_found() const {
    return std::make_pair<ddprof::DsoSetConstIt, bool>(_set.end(), false);
  }

  DsoUID_t find_or_add_dso_uid(const ddprof::Dso &dso);

  /********* Region helpers ***********/
  const ddprof::RegionHolder &find_or_insert_region(const ddprof::Dso &dso);

  ddprof::DsoSet _set;
  ddprof::RegionMap _region_map;
  struct ddprof::DsoStats _stats;
  BackpopulateStateMap _backpopulate_state_map;
  // Associate unique IDs even for different PIDs
  std::unordered_map<ddprof::RegionKey, DsoUID_t> _dso_uid_map;
  DsoUID_t _next_dso_id;
};
