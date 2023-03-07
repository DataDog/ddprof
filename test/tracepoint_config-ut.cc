#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "tracepoint_config.hpp"

namespace ddprof {

TEST(tracepoint_config, getid) {
  LogHandle handle;
  int64_t id = tracepoint_get_id("raw_syscalls", "sys_exit");
  // This can fail without the appropriate permissions
  LG_DBG("Tracepoint: raw_syscall/sys_exit id=%ld ", id);
#if defined(__x86_64__)
  if (id != -1) {
    EXPECT_EQ(id, 348);
  }
#endif
}

} // namespace ddprof
