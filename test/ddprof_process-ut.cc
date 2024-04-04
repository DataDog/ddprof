// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_process.hpp"

#include "loghandle.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

namespace ddprof {
TEST(DDProfProcess, simple_self) {
  LogHandle handle;
  pid_t mypid = getpid();
  Process p(mypid);
  EXPECT_NE(p.get_cgroup_ns(""), Process::kCGroupNsError);
}

TEST(DDProfProcess, no_file) {
  LogHandle handle;
  Process p(1430928460);
  EXPECT_EQ(p.get_cgroup_ns(""), Process::kCGroupNsError);
}

TEST(DDProfProcess, container_id) {
  LogHandle handle;
  std::string path_tests = std::string(UNIT_TEST_DATA) + "/" + "container_id/";
  ContainerId container_id;
  auto ddres =
      extract_container_id(path_tests + "cgroup.kubernetess", container_id);
  EXPECT_TRUE(IsDDResOK(ddres));
  EXPECT_TRUE(container_id);
  if (container_id) {
    LG_DBG("container id %s", container_id.value().c_str());
  }
}

TEST(DDProfProcess, simple_pid_2) {
  LogHandle handle;
  ProcessHdr process_hdr(UNIT_TEST_DATA);
  std::optional<std::string> opt_string = process_hdr.get_container_id(2);
  ASSERT_TRUE(opt_string);
  if (opt_string) {
    LG_DBG("container id %s", opt_string.value().c_str());
  }
}

} // namespace ddprof
