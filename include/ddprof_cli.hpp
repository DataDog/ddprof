#pragma once

#include "ddres_def.hpp"
#include "exporter_input.hpp"

#include <string>
#include <vector>

namespace ddprof {

struct DDProfCLI {
  DDProfCLI() {}
  int parse(int argc, const char *argv[]);
  // Basic options
  ExporterInput_V2 exporter_input;
  std::string tags; // todo allow vector ?

  // Profiling options
  int pid{-1};
  bool global{false};
  unsigned upload_period;
  unsigned profiler_reset; // worker_period
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

  // hidden
  uint64_t cpu_affinity{0};
  bool show_samples{false};
  bool fault_info{true};
  bool help_hidden{false};

  // valid state to continue ?
  bool continue_exec = {false};

  // args
  std::vector<std::string> command_line;
};

} // namespace ddprof
