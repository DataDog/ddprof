// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "span.hpp"

namespace ddprof {

struct RequestMessage {
  enum { kPid = 1, kRingBuffer = 2 };
  uint32_t request = 0;
};

struct ResponseMessage {
  struct Data {
    uint32_t request = 0;
    int32_t pid = -1;
    uint64_t mem_size = 0;
  };
  struct FileDescriptors {
    int mem_fd = -1;
    int event_fd = -1;
  };
  Data data;
  FileDescriptors fds;
};

static constexpr size_t kMaxFD = 253;

bool send(int sfd, const RequestMessage &msg);
bool send(int sfd, const ResponseMessage &msg);
bool receive(int sfd, RequestMessage &msg);
bool receive(int sfd, ResponseMessage &msg);

ssize_t send(int sfd, ddprof::span<const std::byte> buffer,
             ddprof::span<const int> fds = {});
std::pair<ssize_t, size_t> receive(int sfd, ddprof::span<std::byte> buffer,
                                   ddprof::span<int> fds = {});
} // namespace ddprof
