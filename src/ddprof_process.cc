// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_process.hpp"

#include "ddres.hpp"
#include "string_format.hpp"

#include <charconv> // for std::from_chars
#include <unistd.h>
#include <vector>

namespace ddprof {
constexpr auto k_max_buf_cgroup_link = 1024;

std::string Process::format_cgroup_file(pid_t pid,
                                        std::string_view path_to_proc) {
  return string_format("%s/proc/%d/cgroup", path_to_proc.data(), pid);
}

const ContainerId &Process::get_container_id(std::string_view path_to_proc) {
  if (!_container_id) {
    extract_container_id(format_cgroup_file(_pid, path_to_proc), _container_id);
    if (!_container_id) {
      // file can be gone (short lived pid ?)
      // store a value to avoid further lookup
      _container_id = k_container_id_unknown;
    }
  }
  return _container_id;
}

Process::CGroupId_t Process::get_cgroup_ns(std::string_view path_to_proc) {
  if (_cgroup_ns == kCGroupNsNull) {
    read_cgroup_ns(_pid, path_to_proc, _cgroup_ns);
  }
  return _cgroup_ns;
}

DDRes Process::read_cgroup_ns(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup) {
  cgroup = Process::kCGroupNsError;
  std::string const path =
      string_format("%s/proc/%d/ns/cgroup", path_to_proc.data(), pid);
  char buf[k_max_buf_cgroup_link];
  ssize_t const len = readlink(path.c_str(), buf, k_max_buf_cgroup_link - 1);
  if (len == -1) {
    // avoid logging as this is frequent
    return ddres_warn(DD_WHAT_CGROUP);
  }

  buf[len] = '\0'; // null terminate the string

  std::string_view const linkTarget(buf);
  size_t const start = linkTarget.find_last_of('[');
  size_t const end = linkTarget.find_last_of(']');

  if (start == std::string::npos || end == std::string::npos) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to find id %s",
                          linkTarget.data());
  }

  std::string_view const id_str = linkTarget.substr(start + 1, end - start - 1);

  auto [p, ec] =
      std::from_chars(id_str.data(), id_str.data() + id_str.size(), cgroup);
  if (ec == std::errc::invalid_argument ||
      ec == std::errc::result_out_of_range) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to cgroup to number PID%d",
                          pid);
  }
  return {};
}

const ContainerId &ProcessHdr::get_container_id(pid_t pid, bool force) {
  // lookup cgroup
  static const ContainerId unknown_container_id =
      ContainerId(k_container_id_unknown);
  auto it = _process_map.find(pid);
  if (it == _process_map.end()) {
    // new process, parse cgroup
    auto pair = _process_map.try_emplace(pid, pid);
    if (pair.second) {
      it = pair.first;
    } else {
      LG_WRN("[ProcessHdr] Unable to insert process element");
      return unknown_container_id;
    }
  }
  // uint64 is big enough that overflow is not a concern
  // also consequence is just miss-labelling container-id
  if (!force &&
      (it->second.increment_counter() < k_nb_samples_container_id_lookup)) {
    // avoid looking up container_id too often for short-lived pids
    return unknown_container_id;
  }
  return it->second.get_container_id(_path_to_proc);
}

} // namespace ddprof
