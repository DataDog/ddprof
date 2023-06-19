#pragma once

#include "ddres_def.hpp"
#include "exporter_input.hpp"

#include "perf_watcher.hpp"
#include "event_parser.h"

#include <string>
#include <vector>

namespace ddprof {

struct DDProfCLI {
public:
  DDProfCLI() {}

  int parse(int argc, const char *argv[]);

  // based on input events, add the appropriate watchers
  DDRes add_watchers_from_events(std::vector<PerfWatcher> &watcher) const;

  void print() const;
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
  bool version{false}; // request version
  bool enable{true};

  // hidden
  std::string cpu_affinity;
  bool show_samples{false};
  bool fault_info{true};
  bool help_hidden{false};
  int socket{-1};
  // valid state to continue ?
  bool continue_exec = {false};

  // args
  std::vector<std::string> command_line;
private:
  void help_events();
};

} // namespace ddprof
