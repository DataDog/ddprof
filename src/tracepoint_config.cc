// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

namespace ddprof {
// Returns the ID of the given Linux tracepoint, or -1 if an error occurs.
int64_t tracepoint_get_id(std::string_view global_name,
                          std::string_view tracepoint_name) {
  if (global_name.empty() || tracepoint_name.empty()) {
    return -1;
  }

  // todo: should we even consider the debug path ? (is it not deprecated?)
  std::string fs_path;
  struct stat sb;
  if (stat("/sys/kernel/tracing/events", &sb) == 0) {
    fs_path = "/sys/kernel/tracing/events";
  } else if (stat("/sys/kernel/debug/tracing/events", &sb) == 0) {
    fs_path = "/sys/kernel/debug/tracing/events";
  } else if (stat("/proc/1/root/sys/kernel/debug/tracing/events", &sb) == 0) {
    fs_path = "/proc/1/root/sys/kernel/debug/tracing/events";
  } else {
    return -1; // Neither debugfs nor tracefs is available.
  }

  // todo this path could change (from user overrides)
  std::stringstream id_path;
  id_path << fs_path << "/" << global_name << "/" << tracepoint_name << "/id";

  // Read the ID from the file.
  std::ifstream id_file(id_path.str());
  if (!id_file) {
    return -1;
  }
  long trace_id;
  if (!(id_file >> trace_id)) {
    return -1;
  }
  return trace_id;
}
} // namespace ddprof
