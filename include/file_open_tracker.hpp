#pragma once

#include "ddprof_defs.hpp"
#include "logger.hpp"
#include "unwind_output.hpp"

#include <unordered_map>
#include <unordered_set>

#include <signal.h>

namespace ddprof {

class FileOpen {
public:
  void do_open(const UnwindOutput &stack, int fd, pid_t pid) {
    if (fd < 0)  // Don't mark failures
      return;
    StackMap &stack_map = _pid_map[pid];
    stack_map[fd] = stack;
    _visited_recently.insert(pid);
  }


  void do_close(int fd, pid_t pid) {
    if (fd < 0)  // Don't mark failures
      return;
    StackMap &stack_map = _pid_map[pid];
    stack_map.erase(fd);
    _visited_recently.insert(pid);
  }

  void do_exit(pid_t pid) {
    StackMap &stack_map = _pid_map[pid];
    stack_map.clear();
    _visited_recently.erase(pid);
  }

  void sanitize_pids() {
    for (auto &stack_map : _pid_map) {
      if (!_visited_recently.contains(stack_map.first)) {
        // This PID wasn't visited recently.  Is it still around?
        if (kill(stack_map.first, 0)) {
          _pid_map[stack_map.first].clear();
        }
      }
    }
    _visited_recently.clear();
  }

  using StackMap = std::unordered_map<int, UnwindOutput>;
  using PidMap = std::unordered_map<pid_t, StackMap>;

  PidMap _pid_map;
  std::unordered_set<pid_t> _visited_recently;
  int watcher_pos;
};

} // namespace ddprof
