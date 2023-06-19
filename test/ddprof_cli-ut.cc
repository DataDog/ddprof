#include <gtest/gtest.h>

#include "ddprof_cli.hpp"
#include "loghandle.hpp"

namespace ddprof {
TEST(ddprof_cli, help) {
  LogHandle handle;
  const char *argv[] = {{MYNAME}, {"--help"}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_EQ(0, res);
  EXPECT_FALSE(ddprof_cli.continue_exec);
}
TEST(ddprof_cli, help_events) {
  const char *argv[] = {{MYNAME}, {"--event"}, {"help"}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_EQ(0, res);
  EXPECT_FALSE(ddprof_cli.continue_exec);
}

TEST(ddprof_cli, show_hidden) {
  const char *argv[] = {{MYNAME}, {"--help_hidden"}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_EQ(0, res);
  EXPECT_FALSE(ddprof_cli.continue_exec);
}

TEST(ddprof_cli, no_options) {
  LogHandle handle;
  const char *argv[] = {{MYNAME}, {"some"}, {"this"}, {"thing"}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_EQ(0, res);
  EXPECT_EQ(ddprof_cli.command_line[0], "some");
  EXPECT_EQ(ddprof_cli.command_line.size(), 3);
  EXPECT_TRUE(ddprof_cli.continue_exec);
}

TEST(ddprof_cli, hyphen_hyphen) {
  LogHandle handle;
  const char *argv[] = {{MYNAME}, {"--"}, {"this"}, {"thing"}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_EQ(0, res);
  EXPECT_EQ(ddprof_cli.command_line[0], "this");
  EXPECT_EQ(ddprof_cli.command_line.size(), 2);
  EXPECT_TRUE(ddprof_cli.continue_exec);
}

TEST(ddprof_cli, empty) {
  LogHandle handle;
  const char *argv[] = {{MYNAME}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_NE(0, res);
  EXPECT_FALSE(ddprof_cli.continue_exec);
}

TEST(ddprof_cli, global) {
  LogHandle handle;
  const char *argv[] = {{MYNAME}, {"--global"}};
  int argc = sizeof(argv) / sizeof(argv[0]);
  ddprof::DDProfCLI ddprof_cli;
  int res = ddprof_cli.parse(argc, argv);
  EXPECT_EQ(0, res);
  EXPECT_TRUE(ddprof_cli.continue_exec);
}

} // namespace ddprof
