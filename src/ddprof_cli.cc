// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// clang-format off
// https://gitlab.alpinelinux.org/alpine/aports/-/issues/8626
// /usr/include/fortify/stdio.h:73:28: error: inlining failed in call to 'always_inline' 'vsnprintf': function body can be overwritten at link time
// 73 | _FORTIFY_FN(vsnprintf) int vsnprintf(char * _FORTIFY_POS0 __s, size_t __n,
// clang-format on
#undef _FORTIFY_SOURCE

#include "ddprof_cli.hpp"

#include "CLI/CLI11.hpp"
#include "constants.hpp"
#include "ddprof_cmdline_watcher.hpp"
#include "ddprof_defs.hpp"
#include "ddres.hpp"
#include "logger.hpp"
#include "version.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <optional>

namespace ddprof {

namespace {
constexpr size_t k_required_stack_sample_size_alignment{8};
constexpr std::chrono::seconds k_default_upload_period{59};
constexpr size_t k_default_worker_period{240};
constexpr std::chrono::milliseconds k_default_loaded_libs_check_delay{5000};
constexpr std::chrono::milliseconds k_default_loaded_libs_check_interval{59000};

std::string api_key_to_dbg_string(std::string_view value) {
  if (value.size() != k_size_api_key) {
    LG_WRN("API key does not have the expected %u size", k_size_api_key);
  }
  size_t const len = value.length();
  std::string masked(len, '*');
  masked.replace(len - 4, 4, value.substr(len - 4));
  return masked;
}

std::string get_default_config_file() {
  // The CLI docs says config files are compatible with envname, however the
  // _process_env function is called after the file function
  // Hence this hack to set the default to the actual env variable
  if (char *env_config_path = std::getenv("DD_PROFILING_NATIVE_CONFIG");
      env_config_path != nullptr) {
    return env_config_path;
  }
  return "./ddprof.toml";
}
} // namespace

void DDProfCLI::help_events() { std::cout << watcher_help_text(); }

namespace {
void write_config_file(const CLI::App &app, const std::string &file_path) {
  std::ofstream out_file;
  out_file.open(file_path);
  if (!out_file) {
    // logger is not configured
    (void)fprintf(stderr, "ddprof_cli: cannot open the file %s",
                  file_path.c_str());
    return;
  }
  // Write the configuration (include defaults and descriptions)
  // we can't include defaults or this triggers incompatible options
  out_file << app.config_to_str();
  out_file.close();
}

using Validator = CLI::Validator;
struct SampleStackSizeValidator : public Validator {
  SampleStackSizeValidator() {
    name_ = "SAMPLE_STACK_SIZE";
    func_ = [](const std::string &str) {
      int const value = std::stoi(str);
      if (value >= USHRT_MAX ||
          value % k_required_stack_sample_size_alignment != 0) {
        return static_cast<std::string>(
            "Invalid k_required_stack_sample_size_alignment "
            "value. Value should be less than " +
            std::to_string(USHRT_MAX) + " and a multiple of 8.");
      }
      return std::string();
    };
  }
};

struct MaximumPidsValidator : public Validator {
  MaximumPidsValidator() {
    name_ = "MAXIMUM_PIDS";
    func_ = [](const std::string &str) {
      int const value = std::stoi(str);
      if (value <= 0 && value != -1) {
        return static_cast<std::string>(
            "Invalid value for maximum pids.\n"
            "The value should be -1 for an unlimited number of pids.\n"
            "The value should be N to limit to N pids.\n");
      }
      return std::string();
    };
  }
};
} // namespace

int DDProfCLI::parse(int argc, const char *argv[]) {
  std::string capture_config;
  CLI::App app{MYNAME " is a command line utility to gather profiling data and "
                      "visualize it in the Datadog UI.\n"
                      "You can continuously inspect where your application is "
                      "spending CPU and memory."
                      "\n"
                      " eg: " MYNAME " -S service_name -H localhost -P 8192 "
                      "redis-server /etc/redis/redis.conf\n",
               MYNAME}; // avoid auto-generation of NAME (which includes path)

  CLI::Option *exec_option =
      app.add_option("command_line", command_line,
                     "Your command line (including arguments)\n"
                     "This runs profiling on the given command line.\n"
                     "Incompatible with PID or Global modes.");
  app.positionals_at_end();

  // Basic options to surface
  app.add_option("--service,-S", exporter_input.service,
                 "The name of the profiled service."
                 " Profiles are grouped by service.")
      ->default_val("myservice")
      ->envname("DD_SERVICE");

  app.add_option("--environment,-E", exporter_input.environment,
                 "The name of the environment to use in the Datadog UI.")
      ->envname("DD_ENV");

  app.add_option("--service_version,--service-version,-V",
                 exporter_input.service_version,
                 "Version of the service being profiled.")
      ->envname("DD_VERSION");

  app.add_option("--url,-U", exporter_input.url,
                 "A <hostname>:<port> URL.  Either <hostname>:<port>, "
                 "http://<hostname>:<port>\n"
                 "or https://<hostname>:<port> \n"
                 "or unix:///run/apm.sock\n")
      ->envname("DD_TRACE_AGENT_URL");
  app.add_option("--host,-H", exporter_input.host,
                 "The hostname of the agent. This is combined with the port "
                 "option to generate a URL\n")
      ->envname("DD_AGENT_HOST")
      ->default_val("localhost");
  app.add_option("--port,-P", exporter_input.port,
                 "The communication port for the Datadog agent. This is "
                 "combined with the host option to generate a URL\n")
      ->envname("DD_TRACE_AGENT_PORT")
      ->default_val("8126");

  app.add_option("--tags,-T", tags,
                 "Tags attached to the profiler's data\n"
                 "Specified as a list of tags, ie: key1:value1,key2:value2\n")
      ->envname("DD_TAGS");

  // Profiling settings
  CLI::Option *pid_opt =
      app.add_option(
             "--pid,-p", pid,
             "Instrument the given PID rather than launching a new process.")
          ->group("Profiling settings")
          ->excludes(exec_option);
  app.add_flag("--global,-g", global,
               "Instrument all processes.\n"
               "Requires specific capabilities or a perf_event_paranoid "
               "value of less than 1.")
      ->group("Profiling settings")
      ->excludes(pid_opt)
      ->excludes(exec_option);
  app.add_option("--inlined_functions,--inlined-functions,-I",
                 inlined_functions,
                 "Report inlined functions in call stacks.\n"
                 "This is possible if debug sections are available.\n"
                 "This can have performance impacts for the profiler.")
      ->group("Profiling settings")
      ->default_val(false)
      ->envname("DD_PROFILING_INLINED_FUNCTIONS");
  app.add_flag("--timeline,-t", timeline,
               "Enables Timeline view in the Datadog UI.\n"
               "Works by adding timestmaps to certain events.")
      ->group("Profiling settings")
      ->envname("DD_PROFILING_TIMELINE_ENABLE");

  app.add_option<std::chrono::seconds, unsigned>(
         "--upload_period,--upload-period,-u", upload_period,
         "Upload period for profiles (in seconds).\n")
      ->default_val(
          static_cast<std::chrono::seconds>(k_default_upload_period).count())
      ->group("Profiling settings")
      ->envname("DD_PROFILING_UPLOAD_PERIOD");

  app.add_option("--event,-e", events,
                 "Customize the events we instrument."
                 " For more help, use -e help.")
      ->group("Profiling settings")
      // only add 1 element to differentiate with cmd line
      ->allow_extra_args(false)
      ->envname(k_events_env_variable)
      ->delimiter(';');

  app.add_option("--preset", preset,
                 "Select a predefined profiling configuration."
                 "Available presets:\n"
                 "  - default: profile CPU and memory allocations\n"
                 "     (profile only CPU when targeting a given PID)\n"
                 "  - cpu_only: profile CPU\n"
                 "  - alloc_only: profile memory allocations\n"
                 "  - cpu_live_heap: profile live allocations and CPU\n")
      ->group("Profiling settings")
      ->envname("DD_PROFILING_NATIVE_PRESET");

  // Advanced settings
  app.add_option("--switch_user,--switch-user", switch_user,
                 "Run my application with a different user.\n")
      ->group("Advanced settings");
  app.add_option("--nice", nice,
                 "Niceness (priority of process) for the profiler.\n"
                 "Higher value means nicer (lower priority).\n")
      ->group("Advanced settings");

  // allow configuration files - default is local toml file
  app.set_config("--config", get_default_config_file(),
                 "A configuration file\n"
                 "Check the capture_config to generate the initial file")
      ->group("Advanced settings");

  // Debug
  app.add_option("--log_level,--log-level,-l", log_level,
                 "One of debug, informational, notice, warn, error.")
      ->default_val("error")
      ->check(
          CLI::IsMember({"debug", "informational", "notice", "warn", "error"}))
      ->group("Debug options")
      ->envname("DD_PROFILING_NATIVE_LOG_LEVEL");
  //
  app.add_option("--log_mode,--log-mode,-o", log_mode,
                 "One of stdout, stderr, syslog, or disabled.")
      ->default_val("stdout")
      ->group("Debug options")
      ->envname("DD_PROFILING_NATIVE_LOG_MODE");
  //
  app.add_flag("--show_config,--show-config", show_config,
               "Display the configuration.")
      ->default_val(false)
      ->group("Debug options");
  //
  app.add_option("--internal_stats,--internal-stats,-b", internal_stats,
                 "Enables statsd metrics for " MYNAME ". Value should point "
                 "to a statsd socket.\n"
                 "Example: /var/run/datadog-agent/statsd.sock")
      ->group("Debug options")
      ->envname("DD_PROFILING_INTERNAL_STATS");

  app.add_flag("--show_samples,--show-samples", show_samples,
               "Display captured samples as logs.\n")
      ->group("Debug options");
  app.add_flag("--version,-v", version, "Display the profiler's version.\n")
      ->group("Debug options");
  app.add_option("--enable", enable,
                 "Option to disable the profiler.\n"
                 "The profiler then acts as a passthrough.\n")
      ->default_val(true)
      ->envname("DD_PROFILING_ENABLED")
      ->group("Debug options");
  app.add_option("--capture_config,--capture-config", capture_config,
                 "Capture the current configuration to a file.\n"
                 "You can then give this configuration through --config.\n")
      ->group("Debug options");

  // EXTENDED OPTIONS
  std::vector<CLI::Option *> extended_options;
  extended_options.push_back(
      app.add_option(
             "--worker_period,--worker-period", worker_period,
             "Period at which the profiler resets it's internal state.\n"
             "The unit is the number of exports (so default is ~4 hours)")
          ->default_val(k_default_worker_period)
          ->group(""));

  extended_options.push_back(
      app.add_option("--api_key,--api-key", exporter_input.api_key,
                     "A debug option to work without the Datadog agent.\n")
          ->group("")
          ->envname("DD_API_KEY"));

  extended_options.push_back(
      app.add_option("--cpu_affinity,--cpu-affinity", cpu_affinity,
                     "Hexadecimal value of the cpu affinity"
                     " eg: 0xa4")
          ->group(""));

  extended_options.push_back(
      app.add_option("--do_export,--do-export", exporter_input.do_export,
                     "Debug flag to prevent exporting the profiles")
          ->default_val(true)
          ->group(""));
  extended_options.push_back(
      app.add_option("--debug_pprof_prefix,--debug-pprof-prefix",
                     exporter_input.debug_pprof_prefix,
                     "Prefix path to capture pprof files locally")
          ->group("")
          ->envname("DD_PROFILING_PPROF_PREFIX"));
  extended_options.push_back(
      app.add_option("--agentless", exporter_input.agentless,
                     "Allow sending profiles directly to Datadog intake")
          ->group(""));
  extended_options.push_back(app.add_option("--fault_info,--fault-info",
                                            fault_info,
                                            "Log segfault information")
                                 ->default_val(true)
                                 ->group(""));
  extended_options.push_back(app.add_flag("--help_extended,--help-extended",
                                          help_extended,
                                          "Show extended options")
                                 ->group(""));
  extended_options.push_back(
      app.add_option(
             "--socket", socket_path,
             "Override the automatically created socket with a specific path")
          ->envname("DD_PROFILING_NATIVE_SOCKET")
          ->group(""));
  extended_options.push_back(
      app.add_option("--pipefd", pipefd_to_library,
                     "Pipe file descriptor to communicate with library that "
                     "spawned the profiler")
          ->group(""));
  extended_options.push_back(
      app.add_option("--stack_sample_size,--stack-sample-size",
                     default_stack_sample_size,
                     "Sample size for the user's stack."
                     "This setting can help with truncated stack traces."
                     "Maximum value is 65528 (<USHORT_MAX and 8Bytes aligned).")
          ->default_val(k_default_perf_stack_sample_size)
          ->envname("DD_PROFILING_SAMPLE_STACK_USER")
          ->group("")
          ->check(SampleStackSizeValidator()));
  extended_options.push_back(
      app.add_option<std::chrono::milliseconds, unsigned>(
             "--initial-loaded-libs-check-delay,--initial_loaded_libs_check_"
             "delay",
             initial_loaded_libs_check_delay,
             "Initial delay (ms) before check for newly loaded libs.")
          ->default_val(static_cast<std::chrono::milliseconds>(
                            k_default_loaded_libs_check_delay)
                            .count())
          ->check(CLI::NonNegativeNumber)
          ->envname("DD_PROFILING_INITIAL_LOADED_LIBS_CHECK_DELAY")
          ->group(""));
  extended_options.push_back(
      app.add_option<std::chrono::milliseconds, unsigned>(
             "--loaded-libs-check-interval,--loaded_libs_check_interval",
             loaded_libs_check_interval,
             "Interval (ms) between checks for newly loaded libs.")
          ->default_val(static_cast<std::chrono::milliseconds>(
                            k_default_loaded_libs_check_interval)
                            .count())
          ->check(CLI::NonNegativeNumber)
          ->envname("DD_PROFILING_LOADED_LIBS_CHECK_INTERVAL")
          ->group(""));

  extended_options.push_back(
      app.add_flag("--remote-symbolization,--remote_symbolization",
                   remote_symbolization, "Enable remote symbolization")
          ->default_val(false)
          ->envname("DD_PROFILING_REMOTE_SYMBOLIZATION")
          ->group(""));

  extended_options.push_back(
      app.add_flag("--disable-symbolization,--disable_symbolization",
                   disable_symbolization, "Disable symbolization")
          ->default_val(false)
          ->envname("DD_PROFILING_DISABLE_SYMBOLIZATION")
          ->group(""));

  extended_options.push_back(
      app.add_flag("--reorder-events,!--no-reorder-events", reorder_events,
                   "Reorder perf events by timestamp")
          ->default_val(false)
          ->envname("DD_PROFILING_REORDER_EVENTS")
          ->group(""));

  extended_options.push_back(app.add_option("--maximum-pids,--maximum_pids",
                                            maximum_pids,
                                            "Maximum number of profiled PIDs."
                                            "Setting -1 means no limit.")
                                 ->check(MaximumPidsValidator())
                                 ->default_val(k_default_max_profiled_pids)
                                 ->envname("DD_PROFILING_MAXIMUM_PIDS")
                                 ->group(""));
  // Parse
  CLI11_PARSE(app, argc, argv);

  // Dump config file
  if (!capture_config.empty()) {
    write_config_file(app, capture_config);
  }

  // Help on Extended options
  if (help_extended) {
    // Adjust the groups before calling help function
    for (auto *el : extended_options) {
      el->group("Extended options");
    }
    std::cout << app.help() << '\n';
    return static_cast<int>(CLI::ExitCodes::Success);
  }

  // Version then exit
  if (version) {
    print_version();
    return static_cast<int>(CLI::ExitCodes::Success);
  }

  // Help option specifically on events
  if (!events.empty() && events.front() == "help") {
    help_events();
    return static_cast<int>(CLI::ExitCodes::Success);
  }

  // Are we setup to do something ?
  if (command_line.empty() && pid == 0 && !global) {
    (void)fprintf(stderr, "Please specify a target to profile \n");
    return static_cast<int>(CLI::ExitCodes::RequiredError);
  }

  continue_exec = true;
  return static_cast<int>(CLI::ExitCodes::Success);
}

DDRes DDProfCLI::add_watchers_from_events(
    std::vector<PerfWatcher> &watchers) const {
  for (const auto &el : events) {
    if (!watchers_from_str(el.c_str(), watchers, default_stack_sample_size)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                             "Invalid event/tracepoint (%s)", el.c_str());
    }
  }
  return {};
}

void DDProfCLI::print() const {
  auto version_str = str_version();
  PRINT_NFO("Version: %.*s", static_cast<int>(version_str.size()),
            version_str.data());
  PRINT_NFO("Exporter Input:");
  if (!exporter_input.api_key.empty()) {
    std::string const api_key_dbg =
        api_key_to_dbg_string(exporter_input.api_key);
    PRINT_NFO("  - api key: %s", exporter_input.api_key.c_str());
  }
  PRINT_NFO("  - service: %s", exporter_input.service.c_str());
  if (!exporter_input.environment.empty()) {
    PRINT_NFO("  - environment: %s", exporter_input.environment.c_str());
  }
  if (!exporter_input.service_version.empty()) {
    PRINT_NFO("  - service_version: %s",
              exporter_input.service_version.c_str());
  }
  if (!exporter_input.url.empty()) {
    PRINT_NFO("  - url: %s", exporter_input.url.c_str());
  }
  PRINT_NFO("  - host: %s", exporter_input.host.c_str());
  PRINT_NFO("  - port: %s", exporter_input.port.c_str());
  PRINT_NFO("  - do_export: %s", exporter_input.do_export ? "true" : "false");

  if (!tags.empty()) {
    PRINT_NFO("Tags: %s", tags.c_str());
  }
  PRINT_NFO("Profiling options:");
  if (pid) {
    PRINT_NFO("  - pid: %d", pid);
  }
  if (global) {
    PRINT_NFO("  - global: %s", global ? "true" : "false");
  }
  if (!command_line.empty()) {
    std::string command_line_str = "[" + command_line[0];
    std::for_each(std::next(command_line.begin()), command_line.end(),
                  [&command_line_str](const std::string &el) {
                    command_line_str += ", " + el;
                  });
    command_line_str += "]";
    PRINT_NFO("  - command line(wrapper mode): %s", command_line_str.c_str());
  }
  PRINT_NFO("  - upload_period: %lds",
            std::chrono::seconds{upload_period}.count());
  PRINT_NFO("  - worker_period: %d", worker_period);

  if (!events.empty()) {
    PRINT_NFO("  - events:");
    for (const auto &event : events) {
      PRINT_NFO("    - %s", event.c_str());
    }
  }

  if (!preset.empty()) {
    PRINT_NFO("  - preset: %s", preset.c_str());
  }
  PRINT_NFO("Advanced settings:");
  if (!switch_user.empty()) {
    PRINT_NFO("  - switch_user: %s", switch_user.c_str());
  }
  if (nice != -1) {
    PRINT_NFO("  - nice: %d", nice);
  }
  PRINT_NFO("Debug:");
  PRINT_NFO("  - log_level: %s", log_level.c_str());
  PRINT_NFO("  - log_mode: %s", log_mode.c_str());
  PRINT_NFO("  - show_config: %s", show_config ? "true" : "false");
  if (!internal_stats.empty()) {
    PRINT_NFO("  - internal_stats: %s", internal_stats.c_str());
  }
  if (version) {
    PRINT_NFO("  - version: %s", version ? "true" : "false");
  }
  if (!enable) {
    PRINT_NFO("  - enable: %s", enable ? "true" : "false");
  }
  PRINT_NFO("  - inlined_functions: %s", inlined_functions ? "true" : "false");
  if (!cpu_affinity.empty()) {
    PRINT_NFO("  - cpu_affinity: %s", cpu_affinity.c_str());
  }
  if (show_samples) {
    PRINT_NFO("  - show_samples: %s", show_samples ? "true" : "false");
  }
  if (timeline) {
    PRINT_NFO("  - timeline: %s", timeline ? "true" : "false");
  }
  PRINT_NFO("  - fault_info: %s", fault_info ? "true" : "false");

  if (default_stack_sample_size != k_default_perf_stack_sample_size) {
    PRINT_NFO("Extended:");
    PRINT_NFO("  - stack_sample_size: %u", default_stack_sample_size);
  }
}

CommandLineWrapper DDProfCLI::get_user_command_line() const {
  std::vector<char *> cargs;
  cargs.reserve(command_line.size());
  for (const auto &a : command_line) {
    cargs.push_back(strdup(a.c_str()));
  }
  cargs.push_back(nullptr); // execvp expects a null-terminated array.
  return CommandLineWrapper{cargs};
}

void CommandLineWrapper::free_user_command_line(
    std::vector<char *> command_line) {
  for (auto &el : command_line) {
    free(el);
    el = nullptr;
  }
}

} // namespace ddprof
