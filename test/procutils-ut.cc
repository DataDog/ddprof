// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

extern "C" {
#include "procutils.h"
#include <unistd.h>
}

TEST(ProcUtilsTest, proc_read) {

  ProcStatus procstat;
  DDRes res = proc_read(&procstat);
  ASSERT_TRUE(IsDDResOK(res));
  printf("pid: %d\n", procstat.pid);
  printf("rss: %lu\n", procstat.rss);
  printf("user: %lu\n", procstat.utime);
  printf("cuser: %lu\n", procstat.cutime);
}

TEST(ProcUtilsTest, check_file_type) {
  char buf[1024] = {0};
  snprintf(buf, 1024, "/proc/%d/maps", getpid());
  ASSERT_TRUE(check_file_type(buf, S_IFMT));
  ASSERT_FALSE(check_file_type(buf, S_IFDIR));
  snprintf(buf, 1024, "/proc/%d", getpid());
  // directory are also files
  ASSERT_TRUE(check_file_type(buf, S_IFMT));
  ASSERT_TRUE(check_file_type(buf, S_IFDIR));
}

TEST(ProcUtilsTest, inode) {
  {
    char buf[1024] = {0};
    snprintf(buf, 1024, "/proc/%d/maps", getpid());
    int64_t size;
    inode_t inode;

    bool file_found = get_file_inode(buf, &inode, &size);
    ASSERT_TRUE(file_found);
  }
  { // symlinks
    std::string path_procutils_test =
        std::string(IPC_TEST_DATA "/procutils_test.txt");
    std::string path_l_procutils_test =
        std::string(IPC_TEST_DATA "/link_procutils_test.txt");
    int64_t size;
    inode_t inode;
    bool file_found =
        get_file_inode(path_procutils_test.c_str(), &inode, &size);
    ASSERT_TRUE(file_found);
    int64_t size_link;
    inode_t inode_link;
    bool link_found =
        get_file_inode(path_l_procutils_test.c_str(), &inode_link, &size_link);
    ASSERT_TRUE(link_found);
    ASSERT_EQ(inode, inode_link);
    ASSERT_EQ(size, size_link);
  }
}
