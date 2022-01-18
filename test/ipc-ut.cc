// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "ipc.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
}
#include <cstdlib>

#include <gtest/gtest.h>
#include <string>

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
    // delete files if it exists
    unlink(fileName.c_str());
    int fileFd = open(fileName.c_str(), O_CREAT | O_RDWR, 0600);
    // Send something from child
    size_t writeRet = write(fileFd, payload.c_str(), payload.size());
    EXPECT_GT(writeRet, 0);
    EXPECT_TRUE(fileFd != -1);
    EXPECT_TRUE(sendfd(sockets[kChildIdx], &fileFd, 1));
    close(sockets[kChildIdx]);
    close(fileFd);
    return;
  } else {
    // I am a parent
    close(sockets[kChildIdx]);
    int size = 0;
    int *fileDescr;
    fileDescr = getfd(sockets[kParentIdx], &size);
    EXPECT_EQ(size, 1);
    EXPECT_TRUE(fcntl(fileDescr[0], F_GETFD, 0) != -1);
    // reset the cursor
    lseek(fileDescr[0], 0, SEEK_SET);
    char *buffer = (char *)malloc(payload.size());
    int readRet = read(fileDescr[0], buffer, payload.size());
    EXPECT_TRUE(readRet > 0);
    // Check it in the parent
    EXPECT_TRUE(memcmp(payload.c_str(), buffer, payload.size()) == 0);
    close(sockets[kParentIdx]);
    free(buffer);
    close(fileDescr[0]);
    free(fileDescr);
  }
}
