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

#include <string>
#include <vector>

namespace ddprof {

class CommandLineWrapper;

struct DDProfCLI {
public:
  DDProfCLI() { exporter_input.profiler_version = str_version(); }
  int parse(int argc, const char *argv[]);

  // based on input events, add the appropriate watchers
  DDRes add_watchers_from_events(std::vector<PerfWatcher> &watcher) const;

  CommandLineWrapper get_user_command_line() const;

  void print() const;
  // Basic options
  ExporterInput exporter_input;
  std::string tags; // todo allow vector ?

  // Profiling options
  int pid{0};
  bool global{false};
  unsigned upload_period;
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
  uint32_t default_sample_stack_user{k_default_perf_sample_stack_user};
  bool show_samples{false};
  bool fault_info{true};
  bool help_extended{false};
  int socket{-1};
  bool continue_exec{false};

  // args
  std::vector<std::string> command_line;

private:
  void help_events();

  DDProfCLI(const DDProfCLI &) = delete;
  DDProfCLI &operator=(const DDProfCLI &) = delete;
  // we could make it moveable (though not needed afaik)
  DDProfCLI(DDProfCLI &&other) = delete;
  DDProfCLI &operator=(const DDProfCLI &&) = delete;
};

class CommandLineWrapper {
public:
  CommandLineWrapper(std::vector<char *> lines)
      : commandLines(std::move(lines)) {}
  ~CommandLineWrapper() { free_user_command_line(commandLines); }

  CommandLineWrapper(const CommandLineWrapper &) = delete;
  CommandLineWrapper &operator=(const CommandLineWrapper &) = delete;
  CommandLineWrapper(CommandLineWrapper &&) = default;
  CommandLineWrapper &operator=(CommandLineWrapper &&) = default;

  const std::vector<char *> &get() const { return commandLines; }

private:
  std::vector<char *> commandLines;
  static void free_user_command_line(std::vector<char *> command_line);
};

} // namespace ddprof
