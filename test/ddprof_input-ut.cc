extern "C" {
#include "ddprof_input.h"

#include "string_view.h"
}

#include "loghandle.hpp"
#include <gtest/gtest.h>

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
  EXPECT_EQ(strcmp(input.logmode, "stdout"), 0);
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
  //   const char *input_values[] = {MYNAME, "--coredumps", "yes",
  //   "my_program"};

  DDRes res =
      ddprof_input_parse(argc, (char **)input_values, &input, &contine_exec);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_TRUE(contine_exec);
  EXPECT_EQ(strcmp(input.coredumps, "yes"), 0);
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
  EXPECT_EQ(input.nb_parsed_params, 2);
  ddprof_input_free(&input);
}
