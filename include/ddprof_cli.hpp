// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"
#include "event_config.hpp"
#include "exporter_input.hpp"

#include "event_parser.h"
#include "perf_watcher.hpp"
#include "version.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace ddprof {

class CommandLineWrapper;

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct DDProfCLI {
public:
  DDProfCLI() { exporter_input.profiler_version = str_version(); }

  int parse(int argc, const char *argv[]);

  // based on input events, add the appropriate watchers
  DDRes add_watchers_from_events(std::vector<PerfWatcher> &watcher) const;

  [[nodiscard]] CommandLineWrapper get_user_command_line() const;

  void print() const;
  // Basic options
  ExporterInput exporter_input;
  std::string tags; // todo allow vector ?

  // Profiling options
  int pid{0};
  bool global{false};
  bool inlining{true};
  std::chrono::seconds upload_period;
  unsigned worker_period; // worker_period
  std::vector<std::string> events;
  std::string preset;

  // Advanced options
  std::string switch_user;
  int nice{-1};

  // debug
  std::string log_level;
  std::string log_mode;
  bool show_config{false};
  std::string internal_stats;
  bool version{false}; // request version
  bool enable{true};

  // extended
  std::string cpu_affinity;
  uint32_t default_stack_sample_size{k_default_perf_stack_sample_size};
  std::chrono::milliseconds initial_loaded_libs_check_delay{0};
  std::chrono::milliseconds loaded_libs_check_interval{0};

  bool show_samples{false};
  bool fault_info{true};
  bool help_extended{false};
  std::string socket_path;
  int pipefd_to_library{-1};
  bool continue_exec{false};
  bool timeline{false};

  // args
  std::vector<std::string> command_line;

private:
  static void help_events();
};

class CommandLineWrapper {
public:
  explicit CommandLineWrapper(std::vector<char *> lines)
      : commandLines(std::move(lines)) {}
  ~CommandLineWrapper() { free_user_command_line(commandLines); }

  CommandLineWrapper(const CommandLineWrapper &) = delete;
  CommandLineWrapper &operator=(const CommandLineWrapper &) = delete;
  CommandLineWrapper(CommandLineWrapper &&) = default;
  CommandLineWrapper &operator=(CommandLineWrapper &&) = default;

  [[nodiscard]] const std::vector<char *> &get() const { return commandLines; }

private:
  std::vector<char *> commandLines;
  static void free_user_command_line(std::vector<char *> command_line);
};

} // namespace ddprof
