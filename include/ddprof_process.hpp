//
// Created by r1viollet on 30/05/23.
//

#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "logger.hpp"

#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

namespace ddprof {

using ContainerId = std::optional<std::string>;

// Extract container id information
// Expects the path to the /proc/<PID>/cgroup file
ContainerId extract_container_id(const std::string &filepath);

class Process {
public:
  using CGroupId_t = uint64_t;
  static constexpr CGroupId_t kCGroupNsNull =
      std::numeric_limits<CGroupId_t>::max();
  static constexpr CGroupId_t kCGroupNsError =
      std::numeric_limits<CGroupId_t>::max() - 1;

  Process(pid_t pid) : _pid(pid), _cgroup_ns(kCGroupNsNull) {}

  // lazy read of cgroup id
  CGroupId_t get_cgroup_ns(std::string_view path_to_proc = "") {
    if (_cgroup_ns == kCGroupNsNull) {
      read_cgroup_ns(_pid, path_to_proc, _cgroup_ns);
    }
    return _cgroup_ns;
  }

  // lazy read of container id
  ContainerId get_container_id(std::string_view path_to_proc = "") {
    if (!_container_id) {
      _container_id =
          extract_container_id(format_cgroup_file(_pid, path_to_proc));
      if (!_container_id) {
        _container_id = k_default_container_id;
      }
    }
    return _container_id;
  }

  uint64_t _sample_counter = {};

private:
  constexpr static std::string_view k_default_container_id = "undefined";
  std::string format_cgroup_file(pid_t pid, std::string_view path_to_proc);

  static DDRes read_cgroup_ns(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup);

  pid_t _pid;
  CGroupId_t _cgroup_ns;
  ContainerId _container_id;
};

class ProcessHdr {
public:
  ProcessHdr(std::string path_to_proc = "") : _path_to_proc(path_to_proc) {}
  ContainerId get_container_id(pid_t pid, bool force = false);
  void clear(pid_t pid) { _process_map.erase(pid); }

private:
  constexpr static auto k_nb_samples_container_id_lookup = 100;
  using ProcessMap = std::unordered_map<pid_t, Process>;
  ProcessMap _process_map;
  std::string _path_to_proc = {};
};

}; // namespace ddprof
