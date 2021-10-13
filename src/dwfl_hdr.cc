#include "dwfl_hdr.hpp"

extern "C" {
#include "logger.h"
}

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

DDRes DwflWrapper::attach(pid_t pid, const Dwfl_Thread_Callbacks *callbacks,
                          struct UnwindState *us) {

  if (_attached) {
    return ddres_init();
  }
  if (!dwfl_attach_state(_dwfl, NULL, pid, callbacks, us)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_DWFL_LIB_ERROR,
                           "Error attaching dwfl on pid %d (%s)", pid,
                           dwfl_errmsg(-1));
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
