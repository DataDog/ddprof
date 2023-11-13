// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ipc.hpp"

#include "syscalls.hpp"
#include "unique_fd.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace ddprof {

TEST(IPCTest, Positive) {
  // Create a socket pair
  std::string payload = "Interesting test.";

  int sockets[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets), 0);
  UniqueFd parent_socket{sockets[0]};
  UniqueFd child_socket{sockets[1]};

  // Fork
  pid_t child_pid = fork();
  if (!child_pid) {
    UniqueFile tmp_file{std::tmpfile()};
    // I am the child, close parent socket (dupe)
    parent_socket.reset();
    UnixSocket socket(child_socket.release());
    int fileFd = fileno(tmp_file.get());
    // Send something from child
    size_t writeRet = write(fileFd, payload.c_str(), payload.size());
    EXPECT_GT(writeRet, 0);
    EXPECT_NE(fileFd, -1);
    std::byte dummy{1};
    std::error_code ec;
    socket.send({&dummy, 1}, {&fileFd, 1}, ec);
    EXPECT_FALSE(ec);
    exit(0);
  } else {
    // I am a parent
    child_socket.reset();
    UnixSocket socket(parent_socket.release());
    std::byte buf[1];
    std::error_code ec;
    int fds[1] = {-1};
    auto res = socket.receive(buf, fds, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.first, 1);
    EXPECT_EQ(res.second, 1);
    UniqueFd fd{fds[0]};
    EXPECT_NE(fcntl(fd.get(), F_GETFD, 0), -1);
    // reset the cursor
    lseek(fd.get(), 0, SEEK_SET);
    auto buffer = std::make_unique<char[]>(payload.size());
    int readRet = read(fd.get(), buffer.get(), payload.size());
    EXPECT_TRUE(readRet > 0);
    // Check it in the parent
    EXPECT_EQ(memcmp(payload.c_str(), buffer.get(), payload.size()), 0);
    int wstatus;
    EXPECT_EQ(child_pid, waitpid(child_pid, &wstatus, 0));
    EXPECT_TRUE(WIFEXITED(wstatus));
    EXPECT_EQ(WEXITSTATUS(wstatus), 0);
  }
}

TEST(IPCTest, timeout) {
  int sockets[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets), 0);
  UnixSocket sock1(sockets[0]);
  UnixSocket sock2(sockets[1]);
  std::error_code ec;

  auto timeout = 50ms;
  auto timeout_tolerance = 10ms;
  {
    sock2.set_read_timeout(timeout, ec);
    ASSERT_FALSE(ec);
    std::byte buffer[32];

    auto t0 = std::chrono::steady_clock::now();
    ASSERT_EQ(sock2.receive(buffer, ec), 0);
    // timeout measurement is not very accurate
    auto d = std::chrono::steady_clock::now() - t0;
    if (d < timeout) {
      LG_ERR("Read timeout error: errno=%s, duration=%.1fms",
             strerror(ec.value()),
             std::chrono::duration<double, std::milli>(d).count());
    }
    ASSERT_GE(d, timeout - timeout_tolerance);
    ASSERT_TRUE(ec);
  }

  sock1.set_write_timeout(timeout, ec);
  ASSERT_FALSE(ec);
  while (true) {
    // fill up send queue
    std::byte buffer[1024];
    auto t0 = std::chrono::steady_clock::now();
    sock1.send(buffer, ec);
    auto d = std::chrono::steady_clock::now() - t0;
    if (ec) {
      if (d < timeout) {
        LG_ERR("Write timeout error: errno=%s, duration=%.1fms",
               strerror(ec.value()),
               std::chrono::duration<double, std::milli>(d).count());
      }
      ASSERT_GE(d, timeout - timeout_tolerance);
      break;
    }
  }
}

TEST(IPCTest, worker_server) {
  constexpr auto kSocketName = "@foo";
  auto server_socket = create_server_socket(kSocketName);
  ReplyMessage msg;
  msg.request = RequestMessage::kProfilerInfo;
  msg.allocation_profiling_rate = 123;
  msg.initial_loaded_libs_check_delay_ms = 456;
  msg.loaded_libs_check_interval_ms = 789;
  msg.pid = 1234;
  msg.stack_sample_size = 5678;
  msg.allocation_flags = 0xdeadbeef;
  msg.ring_buffer.ring_buffer_type = 17;
  msg.ring_buffer.mem_size = 123456789;
  msg.ring_buffer.event_fd = eventfd(0, 0);
  msg.ring_buffer.ring_fd = memfd_create("foo", 0);

  auto server = start_worker_server(server_socket.get(), msg);
  constexpr auto kNbThreads = 10;
  constexpr auto kNbIterations = 100;
  std::vector<std::jthread> threads;
  for (int i = 0; i < kNbThreads; ++i) {
    threads.push_back(std::jthread{[&]() {
      for (int j = 0; j < kNbIterations; ++j) {
        ReplyMessage info;
        ASSERT_TRUE(IsDDResOK(get_profiler_info(
            create_client_socket(kSocketName), kDefaultSocketTimeout, &info)));
        ASSERT_EQ(info.request, RequestMessage::kProfilerInfo);
        ASSERT_EQ(info.pid, msg.pid);
        ASSERT_EQ(info.allocation_profiling_rate,
                  msg.allocation_profiling_rate);
        ASSERT_EQ(info.initial_loaded_libs_check_delay_ms,
                  msg.initial_loaded_libs_check_delay_ms);
        ASSERT_EQ(info.loaded_libs_check_interval_ms,
                  msg.loaded_libs_check_interval_ms);
        ASSERT_EQ(info.stack_sample_size, msg.stack_sample_size);
        ASSERT_EQ(info.allocation_flags, msg.allocation_flags);
        ASSERT_EQ(info.ring_buffer.ring_buffer_type,
                  msg.ring_buffer.ring_buffer_type);
        ASSERT_EQ(info.ring_buffer.mem_size, msg.ring_buffer.mem_size);
      }
    }});
  }
}

} // namespace ddprof
