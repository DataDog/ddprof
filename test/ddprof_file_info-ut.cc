#include "ddprof_file_info.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include "loghandle.hpp"

namespace ddprof {
TEST(FileInfo, move) {
  LogHandle handle;
  std::string file_path = UNIT_TEST_DATA "/test_int_value.txt";
  FileInfo file_info(file_path, 24, 25);
  FileInfoValue value(std::move(file_info), 1);
  EXPECT_EQ(value._info._path, file_path);
}
} // namespace ddprof
