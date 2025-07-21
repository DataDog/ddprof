// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "container_id.hpp"

#include "ddres.hpp"
#include "logger.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace ddprof {

constexpr static std::string_view UUID_SOURCE =
    "[0-9a-f]{8}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{12}";
constexpr static std::string_view CONTAINER_SOURCE = "[0-9a-f]{64}";
constexpr static std::string_view TASK_SOURCE = "[0-9a-f]{32}-\\d+";

namespace {

// Original cgroup v1 logic - unchanged
std::optional<std::string>
container_id_from_cgroup_v1_line(const std::string &line) {
  std::smatch line_matches;
  std::smatch container_matches;
  const std::regex LINE_REGEX(R"(^\d+:[^:]*:(.+)$)");
  const std::regex CONTAINER_REGEX(
      "(" + std::string(UUID_SOURCE) + "|" + std::string(CONTAINER_SOURCE) +
      "|" + std::string(TASK_SOURCE) + ")(?:.scope)? *$");

  if (std::regex_search(line, line_matches, LINE_REGEX)) {
    std::string const match = line_matches[1].str();
    if (std::regex_search(match, container_matches, CONTAINER_REGEX)) {
      return container_matches[1].str();
    }
  }
  return std::nullopt;
}

// New cgroup v2 logic using mountinfo parsing
std::optional<std::string>
container_id_from_mountinfo(pid_t pid, const std::string &base_proc_path = "") {
  std::string mountinfo_path;
  if (base_proc_path.empty()) {
    mountinfo_path = "/proc/" + std::to_string(pid) + "/mountinfo";
  } else {
    mountinfo_path =
        base_proc_path + "/proc/" + std::to_string(pid) + "/mountinfo";
  }

  std::ifstream mountinfo_file(mountinfo_path);

  if (!mountinfo_file) {
    LG_DBG("Failed to open mountinfo file: %s", mountinfo_path.c_str());
    return std::nullopt;
  }

  // Regex pattern similar to Datadog Agent
  // Matches: .*/([^\s/]+)/(container_id)/[\S]*hostname
  const std::regex MOUNTINFO_REGEX(
      R"(.*/([^\s/]+)/([0-9a-f]{64})/[\S]*hostname)");

  std::string line;
  while (std::getline(mountinfo_file, line)) {
    std::smatch matches;
    if (std::regex_search(line, matches, MOUNTINFO_REGEX)) {
      if (matches.size() >= 3) {
        std::string runtime = matches[1].str();
        std::string container_id = matches[2].str();

        // Skip containerd sandboxes prefix like Datadog Agent does
        if (runtime != "sandboxes") {
          LG_DBG("Found container ID from mountinfo: %s (runtime: %s)",
                 container_id.c_str(), runtime.c_str());
          return container_id;
        }
      }
    }
  }

  LG_DBG("No container ID found in mountinfo: %s", mountinfo_path.c_str());
  return std::nullopt;
}

// Detect if we're using cgroup v2
bool is_cgroup_v2(const std::string &cgroup_content) {
  // cgroup v2 has lines that start with "0::"
  return cgroup_content.find("0::") != std::string::npos;
}

// Extract base proc path from cgroup filepath for consistency with other
// functions
std::string extract_base_proc_path(const std::string &cgroup_filepath) {
  // Expected format: [base_path]/proc/PID/cgroup
  // Extract the base_path part
  std::regex base_path_regex(R"(^(.*)/proc/\d+/cgroup$)");
  std::smatch matches;

  if (std::regex_search(cgroup_filepath, matches, base_path_regex) &&
      matches.size() >= 2) {
    return matches[1].str();
  }

  return ""; // Default to empty (real /proc)
}

} // namespace

DDRes extract_container_id(const std::string &filepath,
                           ContainerId &container_id) {
  container_id = std::nullopt;
  std::ifstream cgroup_file(filepath);
  if (!cgroup_file) {
    LG_DBG("Failed to open file: %s", filepath.c_str());
    return ddres_warn(DD_WHAT_CGROUP);
  }

  // Read entire file content to detect cgroup version
  std::string cgroup_content;
  std::string line;
  while (std::getline(cgroup_file, line)) {
    cgroup_content += line + "\n";
  }

  if (cgroup_content.empty()) {
    LG_DBG("Empty cgroup file: %s", filepath.c_str());
    container_id = k_container_id_none;
    return {};
  }

  bool use_cgroup_v2 = is_cgroup_v2(cgroup_content);
  LG_DBG("Detected cgroup %s for file: %s", use_cgroup_v2 ? "v2" : "v1",
         filepath.c_str());

  if (use_cgroup_v2) {
    // Extract PID from filepath (/proc/PID/cgroup)
    std::regex pid_regex(R"(/proc/(\d+)/cgroup)");
    std::smatch pid_matches;
    pid_t pid = 0;

    if (std::regex_search(filepath, pid_matches, pid_regex) &&
        pid_matches.size() >= 2) {
      try {
        pid = std::stoi(pid_matches[1].str());
        std::string base_proc_path = extract_base_proc_path(filepath);
        container_id = container_id_from_mountinfo(pid, base_proc_path);
      } catch (const std::exception &e) {
        LG_DBG("Failed to parse PID from filepath %s: %s", filepath.c_str(),
               e.what());
      }
    } else {
      LG_DBG("Could not extract PID from filepath: %s", filepath.c_str());
    }
  } else {
    // Use original cgroup v1 logic - process line by line
    std::istringstream content_stream(cgroup_content);
    while (std::getline(content_stream, line)) {
      container_id = container_id_from_cgroup_v1_line(line);
      if (container_id) {
        LG_DBG("Extracted container ID: %s from cgroup v1 line: %s",
               container_id->c_str(), line.c_str());
        return {};
      }
    }
  }

  if (container_id) {
    return {};
  }

  // No container ID found
  container_id = k_container_id_none;
  return {};
}

} // namespace ddprof