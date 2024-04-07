// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "container_id.hpp"
#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "dwfl_wrapper.hpp"
#include "logger.hpp"

#include <limits>
#include <memory>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>

namespace ddprof {

class Process {
public:
  explicit Process(pid_t pid) : _pid(pid), _cgroup_ns(kCGroupNsNull) {}

  using CGroupId_t = uint64_t;
  static constexpr CGroupId_t kCGroupNsNull =
      std::numeric_limits<CGroupId_t>::max();
  static constexpr CGroupId_t kCGroupNsError =
      std::numeric_limits<CGroupId_t>::max() - 1;

  // API only relevant for cgroup v2
  // lazy read of cgroup id
  CGroupId_t get_cgroup_ns(std::string_view path_to_proc = "");

  // lazy read of container id
  const ContainerId &get_container_id(std::string_view path_to_proc = "");

  uint64_t increment_counter() { return ++_sample_counter; }

  [[nodiscard]] DwflWrapper *get_or_insert_dwfl();
  [[nodiscard]] DwflWrapper *get_dwfl();
  [[nodiscard]] const DwflWrapper *get_dwfl() const;

private:
  static std::string format_cgroup_file(pid_t pid,
                                        std::string_view path_to_proc);

  static DDRes read_cgroup_ns(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup);

  std::unique_ptr<DwflWrapper> _dwfl_wrapper{};
  ContainerId _container_id;
  pid_t _pid;
  CGroupId_t _cgroup_ns;
  uint64_t _sample_counter{};
};

class ProcessHdr {
public:
  explicit ProcessHdr(std::string_view path_to_proc = "")
      : _path_to_proc(path_to_proc) {}
  void flag_visited(pid_t pid);
  Process &get(pid_t pid);
  const ContainerId &get_container_id(pid_t pid);
  void clear(pid_t pid) { _process_map.erase(pid); }

  std::vector<pid_t> get_unvisited() const;
  const std::unordered_set<pid_t> &get_visited() const { return _visited_pid; }
  void reset_unvisited();

  unsigned process_count() const { return _process_map.size(); }
  void display_stats() const;

private:
  int get_nb_mod() const;

  std::unordered_set<pid_t> _visited_pid;
  using ProcessMap = std::unordered_map<pid_t, Process>;
  ProcessMap _process_map;
  std::string _path_to_proc = {};
};

}; // namespace ddprof
