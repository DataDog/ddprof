#include "ddprof_file_info.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include "loghandle.hpp"

namespace ddprof {
TEST(FileInfo, move) {
  LogHandle handle;
  std::string file_path = UNIT_TEST_DATA "/test_int_value.txt";
  int fd = open(file_path.c_str(), O_RDONLY);
  FileInfo file_info(file_path, 24, 25);
  FileInfoValue value(std::move(file_info), 1, fd);
  int dup_fd = value._fd;
  {
    auto new_val = std::move(value);
    EXPECT_EQ(new_val._fd, dup_fd);
  }
  EXPECT_EQ(value._fd, -1);
}
} // namespace ddprof
