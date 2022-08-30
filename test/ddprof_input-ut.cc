// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_input.hpp"

#include "constants.hpp"
#include "ddprof_context.hpp"
#include "ddprof_context_lib.hpp"
#include "defer.hpp"
#include "loghandle.hpp"
#include "perf_watcher.hpp"
#include "span.hpp"
#include "string_view.hpp"

#include <gtest/gtest.h>
#include <string_view>

using namespace std::literals;

class InputTest : public ::testing::Test {
protected:
  void SetUp() override {}

  LogHandle _handle;
};

bool s_version_called = false;
void print_version() { s_version_called = true; }
string_view str_version() { return STRING_VIEW_LITERAL("1.2.3"); }

TEST_F(InputTest, default_values) {
  DDProfInput input;
  DDRes res = ddprof_input_default(&input);
  EXPECT_TRUE(IsDDResOK(res));
  const char *env_serv = getenv("DD_SERVICE");
  if (env_serv != NULL) {
    EXPECT_EQ(strcmp(input.exp_input.service, env_serv), 0);
  } else {
    EXPECT_EQ(strcmp(input.exp_input.service, "myservice"), 0);
  }
  EXPECT_EQ(strcmp(input.log_mode, "stdout"), 0);
  ddprof_input_free(&input);
}

TEST_F(InputTest, version_called) {
  DDProfInput input;
  bool contine_exec = true;
  const char *input_values[] = {MYNAME, "-v", "my_program"};
  DDRes res =
      ddprof_input_parse(3, (char **)input_values, &input, &contine_exec);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_TRUE(s_version_called);
  EXPECT_FALSE(contine_exec);
  EXPECT_EQ(input.nb_parsed_params, 2);
  ddprof_input_free(&input);
}

TEST_F(InputTest, single_param) {
  DDProfInput input;
  bool contine_exec;
  int argc = 4;
  const char *input_values[] = {MYNAME, "-m", "yes", "my_program"};
  //   const char *input_values[] = {MYNAME, "--core_dumps", "yes",
  //   "my_program"};

  DDRes res =
      ddprof_input_parse(argc, (char **)input_values, &input, &contine_exec);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_TRUE(contine_exec);
  EXPECT_EQ(strcmp(input.core_dumps, "yes"), 0);
  EXPECT_EQ(input.nb_parsed_params, 3);
  ddprof_print_params(&input);
  ddprof_input_free(&input);
}

TEST_F(InputTest, no_params) {
  DDProfInput input;
  bool contine_exec = false;
  int argc = 2;
  const char *input_values[] = {MYNAME, "my_program"};
  DDRes res =
      ddprof_input_parse(argc, (char **)input_values, &input, &contine_exec);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_TRUE(contine_exec);
  EXPECT_EQ(input.nb_parsed_params, 1);
  ddprof_input_free(&input);
}

TEST_F(InputTest, dump_fixed) {
  DDProfInput input;
  bool contine_exec = true;
  int argc = 3;
  const char *input_values[] = {MYNAME, "--V", "my_program"};
  DDRes res =
      ddprof_input_parse(argc, (char **)input_values, &input, &contine_exec);
  EXPECT_FALSE(IsDDResOK(res));
  EXPECT_FALSE(contine_exec);
  ddprof_input_free(&input);
}

TEST_F(InputTest, event_from_env) {
  defer { unsetenv(k_events_env_variable); };
  {
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "my_program"};
    setenv(k_events_env_variable, "sCPU period=1000", 1);
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);
    EXPECT_EQ(input.nb_parsed_params, 1);
    EXPECT_EQ(input.num_watchers, 1);

    const PerfWatcher *watcher = ewatcher_from_idx(DDPROF_PWE_sCPU);
    EXPECT_NE(nullptr, watcher);
    EXPECT_EQ(watcher->config, input.watchers[0].config);
    EXPECT_EQ(input.watchers[0].sample_period, 1000);
    ddprof_input_free(&input);
  }
  {
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "my_program"};
    setenv(k_events_env_variable, ";", 1);
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);
    EXPECT_EQ(input.nb_parsed_params, 1);
    EXPECT_EQ(input.num_watchers, 0);
    ddprof_input_free(&input);
  }
  {
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "my_program"};
    setenv(k_events_env_variable, ";sCPU period=1000;", 1);
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);
    EXPECT_EQ(input.nb_parsed_params, 1);
    EXPECT_EQ(input.num_watchers, 1);

    const PerfWatcher *watcher = ewatcher_from_idx(DDPROF_PWE_sCPU);
    EXPECT_NE(nullptr, watcher);
    EXPECT_EQ(input.watchers[0].config, watcher->config);
    EXPECT_EQ(input.watchers[0].type, watcher->type);
    EXPECT_EQ(input.watchers[0].sample_period, 1000);
    ddprof_input_free(&input);
  }
  {
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "-e", "hINST per=456", "my_program"};
    setenv(k_events_env_variable, "sCPU period=1000;hCPU period=123", 1);
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);
    EXPECT_EQ(input.nb_parsed_params, 3);
    EXPECT_EQ(input.num_watchers, 3);
    const PerfWatcher *w_scpu = ewatcher_from_idx(DDPROF_PWE_sCPU);
    const PerfWatcher *w_hcpu = ewatcher_from_idx(DDPROF_PWE_hCPU);
    const PerfWatcher *w_hinst = ewatcher_from_idx(DDPROF_PWE_hINST);

    EXPECT_NE(nullptr, w_scpu);
    EXPECT_NE(nullptr, w_hcpu);
    EXPECT_NE(nullptr, w_hinst);
    EXPECT_EQ(w_scpu->config, input.watchers[0].config);
    EXPECT_EQ(w_hcpu->config, input.watchers[1].config);
    EXPECT_EQ(w_hinst->config, input.watchers[2].config);

    EXPECT_EQ(input.watchers[0].sample_period, 1000);
    EXPECT_EQ(input.watchers[1].sample_period, 123);
    EXPECT_EQ(input.watchers[2].sample_period, 456);

    ddprof_input_free(&input);
  }
}

TEST_F(InputTest, duplicate_events) {
  defer { unsetenv(k_events_env_variable); };
  {
    // Duplicate events (except tracepoints) are disallowed
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {
        MYNAME, "-e", "sCPU period=456", "-e", "sCPU period=123", "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_FALSE(IsDDResOK(res));

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
  {
    // Duplicate events (except tracepoints) are disallowed
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "-e", "sCPU per=456", "my_program"};
    setenv(k_events_env_variable, "sCPU per=1000", 1);
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_FALSE(IsDDResOK(res));

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
}

TEST_F(InputTest, presets) {
  {
    // Default preset should be CPU + ALLOC
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_TRUE(IsDDResOK(res));

    ddprof::span watchers{ctx.watchers, static_cast<size_t>(ctx.num_watchers)};

    EXPECT_EQ(watchers.size(), 2);
    EXPECT_NE(std::find_if(watchers.begin(), watchers.end(),
                           [](auto &w) {
                             return w.ddprof_event_type == DDPROF_PWE_sCPU;
                           }),
              watchers.end());

    EXPECT_NE(std::find_if(watchers.begin(), watchers.end(),
                           [](auto &w) {
                             return w.ddprof_event_type == DDPROF_PWE_sALLOC;
                           }),
              watchers.end());

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
  {
    // Default preset for PID mode should be CPU
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "--pid", "1234", "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_TRUE(IsDDResOK(res));

    EXPECT_EQ(ctx.num_watchers, 1);
    EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
  {
    // Check cpu_only preset
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "--preset", "cpu_only", "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_TRUE(IsDDResOK(res));

    ddprof::span watchers{ctx.watchers, static_cast<size_t>(ctx.num_watchers)};

    EXPECT_EQ(ctx.num_watchers, 1);
    EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
  {
    // Check alloc_only preset
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "--preset", "alloc_only",
                                  "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_TRUE(IsDDResOK(res));

    EXPECT_EQ(ctx.num_watchers, 2);
    EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sALLOC);
    EXPECT_EQ(ctx.watchers[1].ddprof_event_type, DDPROF_PWE_sDUM);

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
  {
    // Default preset should not be loaded if an event is given in input
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME, "-e", "sCPU", "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_TRUE(IsDDResOK(res));

    EXPECT_EQ(ctx.num_watchers, 1);
    EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
  {
    // If preset is explicit given in input, then another event with the same
    // name as one of the preset events should override the preset event values
    DDProfInput input;
    bool contine_exec = true;
    const char *input_values[] = {MYNAME,     "-e",      "sCPU per=1234",
                                  "--preset", "default", "my_program"};
    DDRes res = ddprof_input_parse(
        std::size(input_values), (char **)input_values, &input, &contine_exec);

    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_TRUE(contine_exec);

    DDProfContext ctx;
    res = ddprof_context_set(&input, &ctx);
    EXPECT_TRUE(IsDDResOK(res));

    EXPECT_EQ(ctx.num_watchers, 2);
    EXPECT_EQ(ctx.watchers[0].ddprof_event_type, DDPROF_PWE_sCPU);
    EXPECT_EQ(ctx.watchers[0].sample_frequency, 1234);
    EXPECT_EQ(ctx.watchers[1].ddprof_event_type, DDPROF_PWE_sALLOC);

    ddprof_input_free(&input);
    ddprof_context_free(&ctx);
  }
}
