#include "ddprof_process.hpp"

#include "ddres.hpp"
#include "defer.hpp"
#include "string_format.hpp"

#include <charconv> // for std::from_chars
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <vector>

#include <fstream>
#include <optional>
#include <regex>
#include <string>

namespace ddprof {
constexpr auto k_max_buf_cgroup_link = 1024;

constexpr static std::string_view UUID_SOURCE =
    "[0-9a-f]{8}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{12}";
constexpr static std::string_view CONTAINER_SOURCE = "[0-9a-f]{64}";
constexpr static std::string_view TASK_SOURCE = "[0-9a-f]{32}-\\d+";

namespace {
std::optional<std::string> container_id_from_line(const std::string &line) {
  std::smatch line_matches;
  std::smatch container_matches;
  const std::regex LINE_REGEX(R"(^\d+:[^:]*:(.+)$)");
  const std::regex CONTAINER_REGEX(
      "(" + std::string(UUID_SOURCE) + "|" + std::string(CONTAINER_SOURCE) +
      "|" + std::string(TASK_SOURCE) + ")(?:.scope)? *$");

  if (std::regex_search(line, line_matches, LINE_REGEX)) {
    std::string match = line_matches[1].str();
    if (std::regex_search(match, container_matches, CONTAINER_REGEX)) {
      return container_matches[1].str();
    }
  }
  return std::nullopt;
}

std::string format_cgroup_file(pid_t pid, std::string_view path_to_proc) {
  return string_format("%s/proc/%d/cgroup", path_to_proc.data(), pid);
}

} // namespace

std::optional<std::string>
Process::extract_container_id(const std::string &filepath) {
  std::ifstream cgroup_file(filepath);
  if (!cgroup_file) {
    // short lived pids will log a lot here
    LG_DBG("Failed to open file: %s", filepath.data());
    return std::nullopt;
  }
  std::string line;
  while (std::getline(cgroup_file, line)) {
    auto container_id = container_id_from_line(line);
    if (container_id) {
      return container_id;
    }
  }
  return std::nullopt;
}

DDRes Process::read_cgroup_id(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup) {
  cgroup = Process::kCGroupIdError;
  std::string path =
      string_format("%s/proc/%d/ns/cgroup", path_to_proc.data(), pid);
  char buf[k_max_buf_cgroup_link];
  ssize_t len = readlink(path.c_str(), buf, k_max_buf_cgroup_link - 1);
  if (len == -1) {
    // avoid logging as this is frequent
    return ddres_warn(DD_WHAT_CGROUP);
  }

  buf[len] = '\0'; // null terminate the string

  std::string_view linkTarget(buf);
  size_t start = linkTarget.find_last_of('[');
  size_t end = linkTarget.find_last_of(']');

  if (start == std::string::npos || end == std::string::npos) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to find id %s",
                          linkTarget.data());
  }

  std::string_view id_str = linkTarget.substr(start + 1, end - start - 1);

  auto [p, ec] =
      std::from_chars(id_str.data(), id_str.data() + id_str.size(), cgroup);
  if (ec == std::errc::invalid_argument ||
      ec == std::errc::result_out_of_range) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to cgroup to number PID%d",
                          pid);
  }
  return {};
}

std::optional<std::string> ProcessHdr::get_container_id(pid_t pid, bool force) {
  // lookup cgroup
  auto it = _process_map.find(pid);
  if (it == _process_map.end()) {
    // new process, parse cgroup
    auto pair = _process_map.try_emplace(pid, pid);
    if (pair.second) {
      it = pair.first;
    } else {
      return std::nullopt;
    }
  }
  ++(it->second._sample_counter);
  if(!force && (it->second._sample_counter) < k_nb_samples_container_id_lookup) {
    // avoid looking up short-lived pids
    return std::nullopt;
  }
  Process::CGroupId_t cgroup_id = it->second.get_cgroup_id(_path_to_proc);
  if (cgroup_id != Process::kCGroupIdNull) {
    auto it_container = _container_id_map.find(cgroup_id);
    if (it_container == _container_id_map.end()) {
      // insert new container ID
      std::string cgroup_file = format_cgroup_file(pid, _path_to_proc);
      std::optional<std::string> container_id =
          Process::extract_container_id(cgroup_file);
      if (container_id) {
        LG_DBG("New container ID %s, PID%d", container_id->c_str(), pid);
      }
      auto pair = _container_id_map.emplace(cgroup_id,
                                            std::move(container_id));
      if (pair.second) {
        it_container = pair.first;
      } else {
        LG_WRN("Failed to insert container id %d \n", pid);
        return std::nullopt;
      }
    }
    return it_container->second;
  }
  return std::nullopt;
}

} // namespace ddprof
