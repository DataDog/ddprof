// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_process.hpp"

#include "loghandle.hpp"

#include <atomic>
#include <gtest/gtest.h>
#include <unistd.h>

#ifndef gettid
#  include <sys/syscall.h>
// There is not always a glibc wrapper
#  define gettid() syscall(SYS_gettid)
#endif

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

std::atomic<pid_t> s_tid;

void *thread_function(void *) {
  // Set the thread name using pthread API
  pthread_setname_np(pthread_self(), "TestThread");
  // Store the thread ID in the atomic variable
  s_tid.store(syscall(SYS_gettid)); // syscall to get the thread ID
  usleep(100000);
  return nullptr;
}

TEST(DDProfProcess, simple_tid) {
  LogHandle handle;
  pthread_t test_thread;
  int ret = pthread_create(&test_thread, nullptr, thread_function, nullptr);
  ASSERT_EQ(ret, 0) << "Failed to create pthread";
  ProcessHdr process_hdr{};
  Process &p = process_hdr.get(getpid());
  std::string_view s = p.get_or_insert_thread_name(gettid());
  LG_DBG("Main thread name is %s", s.data());
  while (!s_tid.load())
    sched_yield();
  ASSERT_NE(s_tid.load(), 0) << "Thread TID should be set";
  std::string_view s2 = p.get_or_insert_thread_name(s_tid.load());
  LG_DBG("New thread name is %s", s2.data());
  EXPECT_EQ(s2, "TestThread");
  // Wait for the thread to store its TID
  pthread_join(test_thread, nullptr);
}

} // namespace ddprof
