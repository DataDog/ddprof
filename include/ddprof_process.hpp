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

class Process {
public:
  using CGroupId_t = uint64_t;
  static constexpr CGroupId_t kCGroupIdNull =
      std::numeric_limits<CGroupId_t>::max();
  static constexpr CGroupId_t kCGroupIdError =
      std::numeric_limits<CGroupId_t>::max() - 1;

  Process(pid_t pid) : _pid(pid), _cgroup(kCGroupIdNull) {}

  // lazy read of cgroup id
  CGroupId_t get_cgroup_id(std::string_view path_to_proc = "") {
    if (_cgroup == kCGroupIdNull) {
      read_cgroup_id(_pid, path_to_proc, _cgroup);
    }
    return _cgroup;
  }
  static std::optional<std::string>
  extract_container_id(const std::string &filepath);

  uint64_t _sample_counter = {};
private:
  static DDRes read_cgroup_id(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup);

  pid_t _pid;
  CGroupId_t _cgroup;
};

// todo ref counting

class ProcessHdr {
public:
  ProcessHdr(std::string path_to_proc = "") : _path_to_proc(path_to_proc) {}

  std::optional<std::string> get_container_id(pid_t pid, bool force = false);
  void clear(pid_t pid) { _process_map.erase(pid); }
private:
  constexpr static auto k_nb_samples_container_id_lookup = 100;
  using ContainerId = std::optional<std::string>;
  using ProcessMap = std::unordered_map<pid_t, Process>;
  using ContainerIdMap = std::unordered_map<Process::CGroupId_t, ContainerId>;

  ProcessMap _process_map;
  ContainerIdMap _container_id_map;
  std::string _path_to_proc = {};
};

}; // namespace ddprof
