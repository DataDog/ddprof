// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ipc.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

static const int kParentIdx = 0;
static const int kChildIdx = 1;

TEST(IPCTest, Positive) {
  // Create a socket pair
  std::string payload = "Interesting test.";

  int sockets[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets), 0);
  // Fork
  pid_t child_pid = fork();
  if (!child_pid) {
    FILE *tmp_file = std::tmpfile();
    // I am the child, close parent socket (dupe)
    close(sockets[kParentIdx]);
    ddprof::UnixSocket socket(sockets[kChildIdx]);
    int fileFd = fileno(tmp_file);
    // Send something from child
    size_t writeRet = write(fileFd, payload.c_str(), payload.size());
    EXPECT_GT(writeRet, 0);
    EXPECT_NE(fileFd, -1);
    std::byte dummy{1};
    std::error_code ec;
    socket.send({&dummy, 1}, {&fileFd, 1}, ec);
    EXPECT_FALSE(ec);
    close(sockets[kChildIdx]);
    close(fileFd);
    exit(0);
  } else {
    // I am a parent
    close(sockets[kChildIdx]);
    ddprof::UnixSocket socket(sockets[kParentIdx]);
    int fd;
    std::byte buf[1];
    std::error_code ec;
    auto res = socket.receive(buf, {&fd, 1}, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.first, 1);
    EXPECT_EQ(res.second, 1);
    EXPECT_NE(fcntl(fd, F_GETFD, 0), -1);
    // reset the cursor
    lseek(fd, 0, SEEK_SET);
    char *buffer = static_cast<char *>(malloc(payload.size()));
    int readRet = read(fd, buffer, payload.size());
    EXPECT_TRUE(readRet > 0);
    // Check it in the parent
    EXPECT_EQ(memcmp(payload.c_str(), buffer, payload.size()), 0);
    close(sockets[kParentIdx]);
    free(buffer);
    close(fd);
    int wstatus;
    EXPECT_EQ(child_pid, waitpid(child_pid, &wstatus, 0));
    EXPECT_TRUE(WIFEXITED(wstatus));
    EXPECT_EQ(WEXITSTATUS(wstatus), 0);
  }
}

TEST(IPCTest, timeout) {
  int sockets[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets), 0);
  ddprof::UnixSocket sock1(sockets[0]);
  ddprof::UnixSocket sock2(sockets[1]);
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
