#include "ddprof_cli.hpp"

#include "CLI/CLI11.hpp"
#include "ddres.hpp"

namespace ddprof {

int DDProfCLI::parse(int argc, const char *argv[]) {
  CLI::App app{ MYNAME " is a command line utility to gather profiling data and "
                      "visualize it in the Datadog UI.\n"
                      "You can continuously inspect where your application is spending CPU and memory."
               "\n"
               " eg: " MYNAME " -S service_name -H localhost -P 8192 "
               "redis-server /etc/redis/redis.conf\n"};

  CLI::Option *exec_option = app.add_option(
      "command_line", command_line, "Your command line (including arguments)\n"
                              "This runs profiling on the given command line.\n"
                              "Incompatible with PID or Global modes.");
  app.positionals_at_end();

  // Basic options to surface
  app.add_option("--service,-S", exporter_input.environment,
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
                 "or unix://some/uds/socket.sock\n")
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
          ->default_val(-1)
          ->group("Profiling settings")
          ->excludes(exec_option);
  app.add_flag("--global,-g", global,
               "Instrument all processes.\n"
               "Requires specific capabilities or a perf_event_paranoid "
               "value of less than 1.")
      ->default_val(false)
      ->group("Profiling settings")
      ->excludes(pid_opt)
      ->excludes(exec_option);

  app.add_option("--upload_period,-u", upload_period,
                 "Upload period for profiles.\n")
      ->default_val(59)
      ->group("Profiling settings")
      ->envname("DD_PROFILING_UPLOAD_PERIOD");

  app.add_option("--event,-e", events,
                 "Customize the events we instrument."
                 " For more help, use -e help.")
      ->group("Profiling settings");
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
      ->default_val(-1)
      ->group("Advanced settings");

  // Debug
  app.add_option("--log_level,-l", "One of debug, notice, warn, error.")
      ->default_val("error")
      ->group("Debug options");
  //
  app.add_option("--log_mode", log_mode,
                 "log_level, One of stdout, stderr, syslog, or disabled.")
      ->default_val("stdout")
      ->group("Debug options");
  //
  app.add_flag("--show_config", show_config, "Display the configuration.")
      ->default_val(false)
      ->group("Debug options");
  //
  app.add_option("--internal_stats,-b", internal_stats,
                 "Enables statsd metrics for \" MYNAME \". Value should point "
                 "to a statsd socket..\n"
                 "Example: /var/run/datadog-agent/statsd.sock")
      ->group("Debug options");
  app.add_flag("--show_samples", show_samples,
               "Display captured samples as logs.\n")
      ->default_val(false)
      ->group("Debug options");

  // HIDDEN OPTIONS
  std::vector<CLI::Option *> hidden_options;
  hidden_options.push_back(
      app.add_option(
             "--profiler_reset", profiler_reset,
             "Period at which the profiler resets it's internal state.\n")
          ->default_val(240)
          ->group(""));

  hidden_options.push_back(
      app.add_option("--api_key", exporter_input.api_key,
                     "A debug option to work without the Datadog agent.\n")
          ->group("")
          ->envname("DD_API_KEY"));

  hidden_options.push_back(
      app.add_option("--cpu_affinity", cpu_affinity,
                     "Hexadecimal value of the cpu affinity")
          ->group(""));

  hidden_options.push_back(
      app.add_option("--do_export", exporter_input.do_export,
                     "Debug flag to prevent exporting the profiles")
          ->default_val(true)
          ->group(""));
  hidden_options.push_back(
      app.add_option("--debug_pprof_prefix", exporter_input.debug_pprof_prefix,
                     "Prefix path to capture pprof files locally")
          ->group(""));
  hidden_options.push_back(
      app.add_option("--agentless", exporter_input.agentless,
                     "Allow sending profiles directly to Datadog intake")
          ->default_val(false)
          ->group(""));
  hidden_options.push_back(
      app.add_option("--fault_info", fault_info, "Log segfault information")
          ->default_val(true)
          ->group(""));
  hidden_options.push_back(
      app.add_flag("--help_hidden", help_hidden, "Show hidden options")
          ->default_val(false)
          ->group(""));

  // Parse
  CLI11_PARSE(app, argc, argv);

  if (help_hidden) {
    for (auto el : hidden_options) {
      el->group("Hidden options");
    }
    std::cout << app.help() << std::endl;
    return static_cast<int>(CLI::ExitCodes::Success);;
  }

  // Are we setup to do something ?
  if (command_line.empty() && pid == -1 && global == false) {
    fprintf(stderr, "Please specify a target to profile \n");
    return static_cast<int>(CLI::ExitCodes::RequiredError);
  }
  continue_exec = true;
  return static_cast<int>(CLI::ExitCodes::Success);;
}

} // namespace ddprof
