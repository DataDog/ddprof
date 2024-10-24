// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_process.hpp"

#include "ddres.hpp"
#include "unique_fd.hpp"
#include "user_override.hpp"

#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace ddprof {

std::string Process::format_cgroup_file(pid_t pid,
                                        std::string_view path_to_proc) {
  return absl::StrCat(path_to_proc, "/proc/", pid, "/cgroup");
}

DwflWrapper *Process::get_or_insert_dwfl() {
  if (!_dwfl_wrapper) {
    _dwfl_wrapper = std::make_unique<DwflWrapper>();
  }
  return _dwfl_wrapper.get();
}

DwflWrapper *Process::get_dwfl() { return _dwfl_wrapper.get(); }

const DwflWrapper *Process::get_dwfl() const { return _dwfl_wrapper.get(); }

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
      absl::StrCat(path_to_proc, "/proc/", pid, "/ns/cgroup");
  char buf[PATH_MAX];
  ssize_t const len = readlink(path.c_str(), buf, std::size(buf));
  if (len == -1 || len == std::ssize(buf)) {
    // avoid logging as this is frequent
    return ddres_warn(DD_WHAT_CGROUP);
  }

  buf[len] = '\0'; // null terminate the string

  std::string_view const linkTarget(buf);
  size_t const start = linkTarget.find_last_of('[');
  size_t const end = linkTarget.find_last_of(']');

  if (start == std::string::npos || end == std::string::npos || start >= end) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to find id %s", buf);
  }

  std::string_view const id_str = linkTarget.substr(start + 1, end - start - 1);

  if (!absl::SimpleAtoi(id_str, &cgroup)) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to cgroup to number PID%d",
                          pid);
  }
  return {};
}

UniqueFile open_proc_comm(pid_t pid, pid_t tid, const char *path_to_proc = "") {
  const std::string proc_comm_filename =
      absl::StrFormat("%s/proc/%d/task/%d/comm", path_to_proc, pid, tid);
  UniqueFile file{fopen(proc_comm_filename.c_str(), "r"), fclose};
  if (!file) {
    // Check if the file exists
    struct stat info;
    UIDInfo old_uids;
    // warning could user switch create too much overhead ?
    if (stat(proc_comm_filename.c_str(), &info) == 0 &&
        IsDDResOK(user_override(info.st_uid, info.st_gid, &old_uids))) {
      file.reset(fopen(proc_comm_filename.c_str(), "r"));
      // Switch back to the original user
      user_override(old_uids.uid, old_uids.gid);
    }
  }
  return file;
}

std::string_view Process::get_or_insert_thread_name(pid_t tid) {
  // Try to insert an empty string first, to ensure only one lookup happens
  auto [it, inserted] = _thread_name_map.emplace(tid, "");

  // If the thread name is already cached (not inserted), return the cached name
  if (!inserted) {
    return it->second;
  }

  // Attempt to open the comm file for the thread
  const UniqueFile comm_file = open_proc_comm(_pid, tid);
  if (!comm_file) {
    // Leave the empty string we just inserted
    return it->second; // This will return the empty string
  }

  // Thread names in Linux are limited to 16 bytes, though 256 is fine
  char thread_name[256];
  if (fgets(thread_name, sizeof(thread_name), comm_file.get()) == nullptr) {
    return it->second; // This will return the empty string
  }

  // Remove the trailing newline character if present
  const size_t len = strlen(thread_name);
  if (len > 0 && thread_name[len - 1] == '\n') {
    thread_name[len - 1] = '\0';
  }

  // Update the value with the actual thread name
  it->second = thread_name;
  return it->second;
}

const ContainerId &ProcessHdr::get_container_id(pid_t pid) {
  Process &p = get(pid);
  return p.get_container_id(_path_to_proc);
}

void ProcessHdr::flag_visited(pid_t pid) { _visited_pid.insert(pid); }

Process &ProcessHdr::get(pid_t pid) {
  _visited_pid.insert(pid);
  auto it = _process_map.find(pid);
  if (it == _process_map.end()) {
    auto pair = _process_map.emplace(pid, pid);
    return pair.first->second;
  }
  return it->second;
}

void ProcessHdr::reset_unvisited() {
  // clear the list of visited for next cycle
  _visited_pid.clear();
}

std::vector<pid_t> ProcessHdr::get_unvisited() const {
  std::vector<pid_t> pids_remove;
  for (const auto &el : _process_map) {
    if (_visited_pid.find(el.first) == _visited_pid.end()) {
      pids_remove.push_back(el.first);
    }
  }
  return pids_remove;
}

int ProcessHdr::get_nb_mod() const {
  int nb_mods = 0;
  std::for_each(_process_map.begin(), _process_map.end(),
                [&](ProcessMap::value_type const &el) {
                  const auto *dwfl = el.second.get_dwfl();
                  if (dwfl) {
                    nb_mods += dwfl->_ddprof_mods.size();
                  }
                });
  return nb_mods;
}

void ProcessHdr::display_stats() const {
  LG_NTC("PROC_HDR  | %10s | %d", "NB MODS", get_nb_mod());
}

} // namespace ddprof
