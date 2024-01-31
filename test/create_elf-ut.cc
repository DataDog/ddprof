// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "create_elf.hpp"
#include "syscalls.hpp"

#include <fcntl.h>
#include <memory>

namespace ddprof {

TEST(create_elf, create_elf_from_self) {
  auto elf = create_elf_from_self();
  ASSERT_TRUE(elf);
}

TEST(create_elf, create_elf_from_self_memfd) {
  if (getenv("EXEC_PROCESS")) {
    return;
  }

  int fd = memfd_create("foobar", 1U /*MFD_CLOEXEC*/);
  ASSERT_NE(fd, -1);

  int src_fd = ::open("/proc/self/exe", O_RDONLY);
  ASSERT_NE(src_fd, -1);

  struct stat statbuf;
  ASSERT_EQ(fstat(src_fd, &statbuf), 0);

  std::unique_ptr<char[]> buf = std::make_unique<char[]>(statbuf.st_size);
  ASSERT_EQ(read(src_fd, buf.get(), statbuf.st_size), statbuf.st_size);

  ASSERT_EQ(write(fd, buf.get(), statbuf.st_size), statbuf.st_size);
  setenv("EXEC_PROCESS", "1", 1);
  char *argv[] = {nullptr};
  ASSERT_NE(fexecve(fd, argv, environ), -1);
}

} // namespace ddprof
