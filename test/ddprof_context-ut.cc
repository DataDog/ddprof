#include <gtest/gtest.h>

#include "constants.hpp"
#include "ddprof_cli.hpp"
#include "ddprof_context_lib.hpp"
#include "defer.hpp"
#include "loghandle.hpp"
#include "version.hpp"

// mocks
bool s_version_called = false;
void print_version() { s_version_called = true; }
std::string_view str_version() { return "1.2.3"; }

namespace ddprof {

TEST(DDProfContext, default_values) {
  LogHandle handle;
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "program"};
  DDProfContext ctx;
  {
    int res = ddprof_cli.parse(std::size(input_values), input_values);
    ASSERT_EQ(res, 0);
  }
  {
    DDRes res = context_set(ddprof_cli, ctx);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(ctx.watchers.size(), 2);
  }
  const char *env_serv = getenv("DD_SERVICE");
  if (env_serv != NULL) {
    EXPECT_EQ(strcmp(ctx.exp_input.service.c_str(), env_serv), 0);
  } else {
    EXPECT_EQ(strcmp(ctx.exp_input.service.c_str(), "myservice"), 0);
  }
}

TEST(DDProfContext, default_preset_cpu_alloc) {
  // Default preset should be CPU + ALLOC
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  EXPECT_EQ(ctx.watchers.size(), 2);

  auto cpu_it =
      std::find_if(ctx.watchers.begin(), ctx.watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sCPU;
      });
  EXPECT_NE(cpu_it, ctx.watchers.end());

  auto alloc_it =
      std::find_if(ctx.watchers.begin(), ctx.watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sALLOC;
      });
  EXPECT_NE(alloc_it, ctx.watchers.end());
}

TEST(DDProfContext, show_config) {
  LogHandle handle;
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "--show_config", "program"};
  DDProfContext ctx;
  {
    int res = ddprof_cli.parse(std::size(input_values), input_values);
    ASSERT_EQ(res, 0);
  }
  {
    DDRes res = context_set(ddprof_cli, ctx);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(ctx.watchers.size(), 2);
  }
}

TEST(DDProfContext, global) {
  LogHandle handle;
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "--global"};
  DDProfContext ctx;
  {
    int res = ddprof_cli.parse(std::size(input_values), input_values);
    ASSERT_EQ(res, 0);
  }
  {
    DDRes res = context_set(ddprof_cli, ctx);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(ctx.watchers.size(), 1);
    EXPECT_EQ(ctx.params.pid, -1);
  }
}

TEST(DDProfContext, version_called) {
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "-v", "my_program"};
  {
    int res = ddprof_cli.parse(std::size(input_values), input_values);
    ASSERT_EQ(res, 0);
  }
  EXPECT_TRUE(s_version_called);
  EXPECT_FALSE(ddprof_cli.continue_exec);
}

TEST(DDProfContext, alloc_conflict) {
  DDProfCLI ddprof_cli;
  const char *input_values[] = {
      MYNAME,      "--show_config",    "-e",       "sCPU per=1234",
      "-e",        "sALLOC per=11234", "--preset", "cpu_live_heap",
      "my_program"};
  {
    int res = ddprof_cli.parse(std::size(input_values), input_values);
    ASSERT_EQ(res, 0);
    EXPECT_TRUE(ddprof_cli.continue_exec);
  }
  {
    DDProfContext ctx;
    DDRes res = context_set(ddprof_cli, ctx);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(ctx.watchers.size(), 2);
    // todo there is an issue here.
    // the period is not taken into account
    // Today's value: Period: 524288
  }
}

TEST(DDProfContext, preset_with_cpu_event) {
  // If preset is explicitly given in input, then another event with the same
  // name as one of the preset events should override the preset event values
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME,     "-e",      "sCPU per=1234",
                                "--preset", "default", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  EXPECT_EQ(ctx.watchers.size(), 2);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
  EXPECT_EQ(ctx.watchers[0].sample_frequency, 1234);
  EXPECT_EQ(ctx.watchers[1].ddprof_event_type, DDPROF_PWE_sALLOC);
}

TEST(DDProfContext, override_default) {
  // Default preset should not be loaded if an event is given in input
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "-e", "sCPU", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 1);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
}

TEST(DDProfContext, cpu_live_heap_preset) {
  // Check cpu_live_heap preset
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "--preset", "cpu_live_heap",
                                "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 2);
  EXPECT_EQ(ctx.watchers[1].ddprof_event_type, DDPROF_PWE_sALLOC);
  EXPECT_EQ(ctx.watchers[1].output_mode, EventConfMode::kLiveCallgraph);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
  EXPECT_EQ(ctx.watchers[0].output_mode, EventConfMode::kCallgraph);
}

TEST(DDProfContext, manual_live_allocation_setting) {
  // Check manual setting of live allocation
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "-e", "sALLOC mode=l", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  // there is a dummy in addition to the allocations
  ASSERT_EQ(ctx.watchers.size(), 2);
  EXPECT_EQ(ctx.watchers[1].ddprof_event_type, DDPROF_PWE_sALLOC);
  EXPECT_EQ(ctx.watchers[1].output_mode, EventConfMode::kLiveCallgraph);

  log_watcher(&ctx.watchers[0], 0);
  log_watcher(&ctx.watchers[1], 1);
}

TEST(DDProfContext, cpu_only_preset) {
  // Check cpu_only preset
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "--preset", "cpu_only", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 1);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
}

TEST(DDProfContext, pid_exclude_command_line) {
  // Default preset for PID mode should be CPU
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "--pid", "1234", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_NE(res, 0);
}

TEST(DDProfContext, pid_mode) {
  // Default preset for PID mode should be CPU
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "--pid", "1234"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 0);

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 1);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
}

TEST(DDProfContext, test_env_variable) {
  setenv(k_events_env_variable, "sCPU period=1234", 1);
  defer { unsetenv(k_events_env_variable); };

  // Init CLI and parse inputs
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  // Assert parsed parameters
  ASSERT_EQ(ddprof_cli.events.size(), 1);

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 1);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
  EXPECT_EQ(ctx.watchers[0].sample_period, 1234);
}

TEST(DDProfContext, input_option_plus_env_var_events) {
  LogHandle handle;
  setenv(k_events_env_variable, "sCPU per=1000", 1);
  defer { unsetenv(k_events_env_variable); };

  // Init CLI and parse inputs
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "-e", "sCPU per=456", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");
  // Assert parsed parameters
  ASSERT_EQ(ddprof_cli.events.size(), 1);
  // CLI takes precedence
  EXPECT_EQ(ddprof_cli.events.back(), "sCPU per=456");

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));
  ASSERT_EQ(ctx.watchers.size(), 1);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
  EXPECT_EQ(ctx.watchers[0].sample_period, 456);
}

TEST(DDProfContext, duplicate_events_disallowed_2) {
  // Init CLI and parse inputs
  DDProfCLI ddprof_cli;
  const char *input_values[] = {
      MYNAME, "-e", "sCPU period=456", "-e", "sCPU period=123", "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  // Assert parsed parameters
  ASSERT_EQ(ddprof_cli.events.size(), 2);

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_FALSE(IsDDResOK(ddres));
}

TEST(DDProfContext, env_variable_overrides_input) {
  // Set environment variable
  setenv(k_events_env_variable, "sCPU period=1000;hCPU period=123", 1);
  defer { unsetenv(k_events_env_variable); };

  // Init CLI and parse inputs
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  // Assert parsed parameters
  ASSERT_EQ(ddprof_cli.events.size(), 2);

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 2);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
  EXPECT_EQ(ctx.watchers[0].sample_period, 1000);
  EXPECT_EQ(ctx.watchers[1].ddprof_event_type, DDPROF_PWE_hCPU);
  EXPECT_EQ(ctx.watchers[1].sample_period, 123);
}

TEST(DDProfContext, env_variable_with_extra_semicolons) {
  // Set environment variable
  setenv(k_events_env_variable, ";sCPU period=1000;", 1);
  defer { unsetenv(k_events_env_variable); };

  // Init CLI and parse inputs
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "my_program"};

  int res = ddprof_cli.parse(std::size(input_values), input_values);
  ASSERT_EQ(res, 0);
  ASSERT_EQ(ddprof_cli.command_line.size(), 1);
  EXPECT_EQ(ddprof_cli.command_line.back(), "my_program");

  // Assert parsed parameters
  ASSERT_EQ(ddprof_cli.events.size(), 1);

  DDProfContext ctx;
  DDRes ddres = context_set(ddprof_cli, ctx);
  EXPECT_TRUE(IsDDResOK(ddres));

  ASSERT_EQ(ctx.watchers.size(), 1);
  EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
  EXPECT_EQ(ctx.watchers[0].sample_period, 1000);
}

} // namespace ddprof
