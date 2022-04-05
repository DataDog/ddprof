// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.h"
#include "span.hpp"

#include <chrono>
#include <system_error>

namespace ddprof {

// Maximum number of file descriptors that can be tansferred with
// sendmsg/recvmsg. Taken from default value for SCM_MAX_FD, which is a kernel
// configuration. 253 is a common safe lower bound for that value.
static constexpr size_t kMaxFD = 253;

static constexpr auto kDefaultSocketTimeout = std::chrono::seconds{1};

using socket_t = int;

using Buffer = ddprof::span<std::byte>;
using ConstBuffer = ddprof::span<const std::byte>;

class UnixSocket {
public:
  static inline constexpr socket_t kInvalidSocket = -1;
  UnixSocket() noexcept : _handle(kInvalidSocket) {}
  explicit UnixSocket(socket_t handle) noexcept : _handle(handle) {}
  UnixSocket(UnixSocket &&socket) noexcept : _handle(socket._handle) {
    socket._handle = kInvalidSocket;
  }

  UnixSocket(const UnixSocket &) = delete;
  ~UnixSocket();

  UnixSocket &operator=(const UnixSocket &) = delete;
  UnixSocket &operator=(UnixSocket &&socket) noexcept {
    std::swap(socket._handle, _handle);
    return *this;
  }

  void close(std::error_code &ec) noexcept;
  void set_write_timeout(std::chrono::microseconds duration,
                         std::error_code &ec) noexcept;
  void set_read_timeout(std::chrono::microseconds duration,
                        std::error_code &ec) noexcept;

  size_t send(ConstBuffer buffer, std::error_code &ec) noexcept;
  size_t send(ConstBuffer buffer, ddprof::span<const int> fds,
              std::error_code &ec) noexcept;

  std::pair<size_t, size_t> receive(Buffer buffer, ddprof::span<int> fds,
                                    std::error_code &ec) noexcept;

  size_t receive(Buffer buffer, std::error_code &ec) noexcept;

  socket_t release() noexcept {
    socket_t h = _handle;
    _handle = kInvalidSocket;
    return h;
  }

private:
  socket_t _handle;
};

struct RequestMessage {
  // Request flags
  enum { kPid = 1, kRingBuffer = 2 };
  // request is bit mask of request flags
  uint32_t request = 0;
};

struct RingBufferInfo {
  uint64_t mem_size = 0;
  int mem_fd = -1;
  int event_fd = -1;
};

struct ReplyMessage {
  // reply with the request flags from the request
  uint32_t request = 0;
  // pid is returned if request & kPid
  int32_t pid = -1;
  // RingBufferInfo is returned if request & kRingBuffer
  RingBufferInfo ring_buffer;
};

class Client {
public:
  explicit Client(UnixSocket &&socket,
                  std::chrono::microseconds timeout = kDefaultSocketTimeout);

  pid_t get_profiler_pid();
  RingBufferInfo get_ring_buffer();

private:
  UnixSocket _socket;
};

class Server {
public:
  explicit Server(UnixSocket &&socket,
                  std::chrono::microseconds timeout = kDefaultSocketTimeout);

  void waitForRequest();

private:
  UnixSocket _socket;
};

} // namespace ddprof
