// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "container_id.hpp"
#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "logger.hpp"

#include <limits>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

typedef void *austin_handle_t;

namespace ddprof {

class Process {
public:
  explicit Process(pid_t pid) : _pid(pid), _cgroup_ns(kCGroupNsNull) {}
  ~Process();
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

  austin_handle_t get_austin_handle(pid_t tid);

  uint64_t _sample_counter = {};

  std::vector<int> _python_register_indices;

private:
  std::string format_cgroup_file(pid_t pid, std::string_view path_to_proc);

  static DDRes read_cgroup_ns(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup);

  pid_t _pid;
  CGroupId_t _cgroup_ns;
  ContainerId _container_id;

  austin_handle_t _austin_handle = nullptr;
};

class ProcessHdr {
public:
  ProcessHdr(std::string_view path_to_proc = "")
      : _path_to_proc(path_to_proc) {}
  const ContainerId &get_container_id(pid_t pid, bool force = false);
  void clear(pid_t pid) { _process_map.erase(pid); }

  Process &get_process(pid_t pid);

private:
  constexpr static auto k_nb_samples_container_id_lookup = 100;
  using ProcessMap = std::unordered_map<pid_t, Process>;
  ProcessMap _process_map;
  std::string _path_to_proc = {};
};

}; // namespace ddprof
