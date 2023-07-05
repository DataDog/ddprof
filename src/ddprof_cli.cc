// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cli.hpp"

#include "CLI/CLI11.hpp"
#include "constants.hpp"
#include "ddprof_cmdline.hpp"
#include "ddres.hpp"
#include "version.hpp"

#include "ddprof_defs.hpp"
#include "logger.hpp"

#include <fstream>
#include <optional>
#include <string.h>

namespace ddprof {

namespace {
std::string api_key_to_dbg_string(std::string_view value) {
  if (value.size() != k_size_api_key) {
    LG_WRN("API key does not have the expected %u size", k_size_api_key);
  }
  size_t len = value.length();
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
    fprintf(stderr, "ddprof_cli: cannot open the file %s", file_path.c_str());
    return;
  }
  // Write the configuration (include defaults and descriptions)
  // we can't include defaults or this triggers incompatible options
  out_file << app.config_to_str();
  out_file.close();
}
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

  app.add_option("--service_version,-V", exporter_input.service_version,
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
      ->envname("DD_AGENT_HOST")
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

  app.add_option("--upload_period,-u", upload_period,
                 "Upload period for profiles (in seconds).\n")
      ->default_val(59)
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
  app.add_option("--switch_user", switch_user,
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
  app.add_option("--log_level,-l", log_level,
                 "One of debug, notice, warn, error.")
      ->default_val("error")
      ->group("Debug options")
      ->envname("DD_PROFILING_NATIVE_LOG_LEVEL");
  //
  app.add_option("--log_mode,-o", log_mode,
                 "log_level, One of stdout, stderr, syslog, or disabled.")
      ->default_val("stdout")
      ->group("Debug options")
      ->envname("DD_PROFILING_NATIVE_LOG_MODE");
  //
  app.add_flag("--show_config", show_config, "Display the configuration.")
      ->default_val(false)
      ->group("Debug options");
  //
  app.add_option("--internal_stats,-b", internal_stats,
                 "Enables statsd metrics for " MYNAME ". Value should point "
                 "to a statsd socket.\n"
                 "Example: /var/run/datadog-agent/statsd.sock")
      ->group("Debug options")
      ->envname("DD_PROFILING_INTERNAL_STATS");

  app.add_flag("--show_samples", show_samples,
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
  app.add_option("--capture_config", capture_config,
                 "Capture the current configuration to a file.\n"
                 "You can then give this configuration through --config.\n")
      ->group("Debug options");

  // EXTENDED OPTIONS
  std::vector<CLI::Option *> extended_options;
  extended_options.push_back(
      app.add_option(
             "--worker_period", worker_period,
             "Period at which the profiler resets it's internal state.\n"
             "The unit is the number of exports (so default is ~4 hours)")
          ->default_val(240)
          ->group(""));

  extended_options.push_back(
      app.add_option("--api_key", exporter_input.api_key,
                     "A debug option to work without the Datadog agent.\n")
          ->group("")
          ->envname("DD_API_KEY"));

  extended_options.push_back(
      app.add_option("--cpu_affinity", cpu_affinity,
                     "Hexadecimal value of the cpu affinity"
                     " eg: 0xa4")
          ->group(""));

  extended_options.push_back(
      app.add_option("--do_export", exporter_input.do_export,
                     "Debug flag to prevent exporting the profiles")
          ->default_val(true)
          ->group(""));
  extended_options.push_back(
      app.add_option("--debug_pprof_prefix", exporter_input.debug_pprof_prefix,
                     "Prefix path to capture pprof files locally")
          ->group("")
          ->envname("DD_PROFILING_PPROF_PREFIX"));
  extended_options.push_back(
      app.add_option("--agentless", exporter_input.agentless,
                     "Allow sending profiles directly to Datadog intake")
          ->group(""));
  extended_options.push_back(
      app.add_option("--fault_info", fault_info, "Log segfault information")
          ->default_val(true)
          ->group(""));
  extended_options.push_back(
      app.add_flag("--help_extended", help_extended, "Show extended options")
          ->group(""));
  extended_options.push_back(
      app.add_option("--socket", socket,
                     "Profiler's IPC socket, as a file descriptor")
          ->envname("DD_PROFILING_NATIVE_SOCKET")
          ->group(""));
  extended_options.push_back(
      app.add_option("--sample_stack_user", default_sample_stack_user,
                     "Default sample size for the user's stack."
                     "This setting can help with truncated stack traces.")
          ->default_val(k_default_perf_sample_stack_user)
          ->envname("DD_PROFILING_SAMPLE_STACK_USER")
          ->group(""));

  // Parse
  CLI11_PARSE(app, argc, argv);

  // Dump comfig file
  if (!capture_config.empty()) {
    write_config_file(app, capture_config);
  }

  // Help on Extended options
  if (help_extended) {
    // Adjust the groups before calling help function
    for (auto el : extended_options) {
      el->group("Extended options");
    }
    std::cout << app.help() << std::endl;
    return static_cast<int>(CLI::ExitCodes::Success);
  }

  // Version then exit
  if (version) {
    print_version();
    return static_cast<int>(CLI::ExitCodes::Success);
  }

  // Help option specifically on events
  if (events.size() && events.front() == "help") {
    help_events();
    return static_cast<int>(CLI::ExitCodes::Success);
  }

  // Are we setup to do something ?
  if (command_line.empty() && pid == 0 && global == false) {
    fprintf(stderr, "Please specify a target to profile \n");
    return static_cast<int>(CLI::ExitCodes::RequiredError);
  }

  continue_exec = true;
  return static_cast<int>(CLI::ExitCodes::Success);
}

DDRes DDProfCLI::add_watchers_from_events(
    std::vector<PerfWatcher> &watchers) const {
  for (const auto &el : events) {
    if (!watchers_from_str(el.c_str(), watchers, default_sample_stack_user)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                             "Invalid event/tracepoint (%s)", el.c_str());
    }
  }
  return {};
}

void DDProfCLI::print() const {
  PRINT_NFO("Version: %s", str_version().data());
  PRINT_NFO("Exporter Input:");
  if (!exporter_input.api_key.empty()) {
    std::string api_key_dbg = api_key_to_dbg_string(exporter_input.api_key);
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
    PRINT_NFO("  - command line(wrapper mode): %s", command_line[0].c_str());
  }
  PRINT_NFO("  - upload_period: %d", upload_period);
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
  if (!cpu_affinity.empty()) {
    PRINT_NFO("  - cpu_affinity: %s", cpu_affinity.c_str());
  }
  if (show_samples) {
    PRINT_NFO("  - show_samples: %s", show_samples ? "true" : "false");
  }
  PRINT_NFO("  - fault_info: %s", fault_info ? "true" : "false");

  if (default_sample_stack_user != k_default_perf_sample_stack_user) {
    PRINT_NFO("Extended:");
    PRINT_NFO("  - sample_stack_user: %u", default_sample_stack_user);
  }
}

CommandLineWrapper DDProfCLI::get_user_command_line() const {
  std::vector<char *> cargs;
  for (const auto &a : command_line) {
    cargs.push_back(strdup(a.c_str()));
  }
  cargs.push_back(nullptr); // execvp expects a null-terminated array.
  return CommandLineWrapper(cargs);
}

void CommandLineWrapper::free_user_command_line(
    std::vector<char *> command_line) {
  for (auto &el : command_line) {
    free(el);
    el = nullptr;
  }
}
} // namespace ddprof
