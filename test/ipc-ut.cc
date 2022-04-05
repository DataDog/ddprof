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

static const int kParentIdx = 0;
static const int kChildIdx = 1;

TEST(IPCTest, Positive) {
  // Create a socket pair
  std::string fileName = IPC_TEST_DATA "/ipc_test_data_Positive.txt";
  std::string payload = "Interesting test.";

  int sockets[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets), 0);
  // Fork
  pid_t child_pid = fork();
  if (!child_pid) {
    // I am the child, close parent socket (dupe)
    close(sockets[kParentIdx]);
    ddprof::UnixSocket socket(sockets[kChildIdx]);
    // delete files if it exists
    unlink(fileName.c_str());
    int fileFd = open(fileName.c_str(), O_CREAT | O_RDWR, 0600);
    // Send something from child
    size_t writeRet = write(fileFd, payload.c_str(), payload.size());
    EXPECT_GT(writeRet, 0);
    EXPECT_NE(fileFd, -1);
    std::byte dummy{1};
    std::error_code ec;
    EXPECT_EQ(socket.send({&dummy, 1}, {&fileFd, 1}, ec), 1);
    EXPECT_FALSE(ec);
    close(sockets[kChildIdx]);
    close(fileFd);
    return;
  } else {
    // I am a parent
    close(sockets[kChildIdx]);
    ddprof::UnixSocket socket(sockets[kParentIdx]);
    int fd;
    std::byte buf[32];
    std::error_code ec;
    auto res = socket.receive(buf, {&fd, 1}, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.first, 1);
    EXPECT_EQ(res.second, 1);
    EXPECT_NE(fcntl(fd, F_GETFD, 0), -1);
    // reset the cursor
    lseek(fd, 0, SEEK_SET);
    char *buffer = (char *)malloc(payload.size());
    int readRet = read(fd, buffer, payload.size());
    EXPECT_TRUE(readRet > 0);
    // Check it in the parent
    EXPECT_EQ(memcmp(payload.c_str(), buffer, payload.size()), 0);
    close(sockets[kParentIdx]);
    free(buffer);
    close(fd);
  }
}

TEST(IPCTest, timeout) {
  int sockets[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets), 0);
  ddprof::UnixSocket sock1(sockets[0]);
  ddprof::UnixSocket sock2(sockets[1]);
  std::error_code ec;

  auto timeout = std::chrono::milliseconds{10};
  {
    sock2.set_read_timeout(timeout, ec);
    ASSERT_FALSE(ec);
    std::byte buffer[32];

    auto t0 = std::chrono::steady_clock::now();
    ASSERT_EQ(sock2.receive(buffer, ec), 0);
    auto d = std::chrono::steady_clock::now() - t0;
    ASSERT_GE(d, timeout);
    ASSERT_TRUE(ec);
  }

  sock1.set_write_timeout(timeout, ec);
  ASSERT_FALSE(ec);
  while (true) {
    // fill up send queue
    std::byte buffer[1024];
    auto t0 = std::chrono::steady_clock::now();
    auto r = sock1.send(buffer, ec);
    auto d = std::chrono::steady_clock::now() - t0;
    if (ec) {
      ASSERT_GE(d, timeout);
      break;
    }
    ASSERT_EQ(r, sizeof(buffer));
  }
}
