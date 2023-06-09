// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "container_id.hpp"

#include "ddres.hpp"
#include "logger.hpp"

#include <fstream>
#include <regex>

namespace ddprof {

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

} // namespace

DDRes extract_container_id(const std::string &filepath,
                           ContainerId &container_id) {
  container_id = std::nullopt;
  std::ifstream cgroup_file(filepath);
  if (!cgroup_file) {
    // short lived pids can fail in this case
    LG_DBG("Failed to open file: %s", filepath.data());
    return ddres_warn(DD_WHAT_CGROUP);
  }
  std::string line;
  while (std::getline(cgroup_file, line)) {
    container_id = container_id_from_line(line);
    if (container_id) {
      return {};
    }
  }
  // exit path in case we are not within a container
  container_id = k_container_id_none;
  return {};
}

} // namespace ddprof