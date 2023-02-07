#include <gtest/gtest.h>

#include "jit/jitdump.hpp"
#include "loghandle.hpp"

namespace ddprof {

TEST(JITTest, SimpleRead) {
  LogHandle handle;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + std::string("jit.dump");
  JITDump jit_dump;
  DDRes res = jit_read(jit_path, jit_dump);
  ASSERT_TRUE(IsDDResOK(res));

}
} // namespace ddprof
