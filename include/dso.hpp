#pragma once

extern "C" {
#include "ddprof_defs.h"
}

// C++ flat set (less allocs than std::set)
#include <amc/flatset.hpp>
#include <array>
#include <iostream>
#include <string>
#include <utility>

#include "region_holder.hpp"

// Out of namespace to allow holding it in C object

namespace ddprof {

// DSO definition
class Dso {
public:
  Dso();
  // pid, start, end, offset, filename (copied once to avoid creating 3
  // different APIs)
  Dso(pid_t pid, ElfAddress_t start, ElfAddress_t end, ElfAddress_t pgoff = 0,
      std::string &&filename = "", bool executable = true);
  // copy parent and update pid
  Dso(const Dso &parent, pid_t new_pid) : Dso(parent) { _pid = new_pid; }

  // Check if the provided address falls within the provided dso
  bool is_within(pid_t pid, ElfAddress_t addr) const;
  bool errored() const { return _errored; }
  bool operator<(const Dso &o) const;
  // Avoid use of strict == as we do not consider _end in comparison
  bool operator==(const Dso &o) const = delete;
  // perf gives larger regions than proc maps (keep the largest of them)
  bool same_or_smaller(const Dso &o) const;
  bool intersects(const Dso &o) const;
  std::string to_string() const;
  void flag_error() const { _errored = true; }

  pid_t _pid;
  ElfAddress_t _start;
  ElfAddress_t _end;
  ElfAddress_t _pgoff;
  std::string _filename;
  DsoUID_t _id;
  dso::DsoType _type;
  bool _executable;

  mutable bool _errored;
};

std::ostream &operator<<(std::ostream &os, const Dso &dso);

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
  bool pid_backpopulate(int);

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

  DsoFindRes find_res_not_found() {
    return std::make_pair<ddprof::DsoSetConstIt, bool>(_set.end(), false);
  }

  /********* Region helpers ***********/
  const ddprof::RegionHolder &find_or_insert_region(const ddprof::Dso &dso);

  ddprof::DsoSet _set;
  ddprof::RegionMap _region_map;
  struct ddprof::DsoStats _stats;
  BackpopulateStateMap _backpopulate_state_map;
  DsoUID_t _next_dso_id;
};
