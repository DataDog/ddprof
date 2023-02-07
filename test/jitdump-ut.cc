#include <gtest/gtest.h>

#include "jit/jitdump.hpp"
#include "loghandle.hpp"

namespace ddprof {

TEST(JITTest, SimpleRead) {
  LogHandle handle;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + std::string("jit.dump");
  JITDump jit_dump;
  DDRes res = jitdump_read(jit_path, jit_dump);
  ASSERT_TRUE(IsDDResOK(res));
  EXPECT_EQ(jit_dump.header.version, k_jit_header_version);
  EXPECT_EQ(jit_dump.code_load.size(), 13);
  EXPECT_EQ(jit_dump.debug_info.size(), 8);
}
} // namespace ddprof
