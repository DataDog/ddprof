#include <gtest/gtest.h>

#include "version.hpp"
#include "ddprof_cli.hpp"
#include "ddprof_context_lib.hpp"
#include "loghandle.hpp"

// mocks
bool s_version_called = false;
void print_version() { s_version_called = true; }
string_view str_version() { return STRING_VIEW_LITERAL("1.2.3"); }

namespace ddprof {
TEST(DDProfContext, default_values) {
  LogHandle handle;
  DDProfCLI ddprof_cli;
  const char *input_values[] = {MYNAME, "program"};
  DDProfContext_V2 ctx;
  {
    int res = ddprof_cli.parse(std::size(input_values), input_values);
    EXPECT_EQ(res, 0);
  }
  {
    DDRes res = context_set_v2(ddprof_cli, ctx);
    EXPECT_TRUE(IsDDResOK(res));
  }
}
}
