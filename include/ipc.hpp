// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_buffer.hpp"
#include "ddres.hpp"
#include "span.hpp"

#include <chrono>
#include <functional>
#include <system_error>

namespace ddprof {

// Maximum number of file descriptors that can be transferred with
// sendmsg/recvmsg. Taken from default value for SCM_MAX_FD, which is a kernel
// configuration. 253 is a common safe lower bound for that value.
static constexpr size_t kMaxFD = 253;

static constexpr auto kDefaultSocketTimeout = std::chrono::seconds{2};

using socket_t = int;

class UnixSocket {
public:
  UnixSocket() noexcept = default;

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

  void send(ConstBuffer buffer, std::error_code &ec) noexcept;
  size_t send_partial(ConstBuffer buffer, std::error_code &ec) noexcept;
  void send(ConstBuffer buffer, ddprof::span<const int> fds,
            std::error_code &ec) noexcept;
  size_t send_partial(ConstBuffer buffer, ddprof::span<const int> fds,
                      std::error_code &ec) noexcept;

  std::pair<size_t, size_t> receive(Buffer buffer, ddprof::span<int> fds,
                                    std::error_code &ec) noexcept;

  std::pair<size_t, size_t> receive_partial(Buffer buffer,
                                            ddprof::span<int> fds,
                                            std::error_code &ec) noexcept;

  size_t receive(Buffer buffer, std::error_code &ec) noexcept;
  size_t receive_partial(Buffer buffer, std::error_code &ec) noexcept;

  socket_t release() noexcept {
    socket_t h = _handle;
    _handle = kInvalidSocket;
    return h;
  }

private:
  static constexpr socket_t kInvalidSocket = -1;

  socket_t _handle = kInvalidSocket;
};

struct RequestMessage {
  // Request flags
  enum { kProfilerInfo = 1 };
  // request is bit mask of request flags
  uint32_t request = 0;
};

struct RingBufferInfo {
  int64_t mem_size = -1;
  int ring_fd = -1;
  int event_fd = -1;
  int ring_buffer_type = 0;
};

struct ReplyMessage {
  // reply with the request flags from the request
  uint32_t request = 0;
  // profiler pid
  int32_t pid = -1;
  int64_t allocation_profiling_rate = 0;
  // RingBufferInfo is returned if request & kRingBuffer
  // cppcheck-suppress unusedStructMember
  RingBufferInfo ring_buffer;
};

class Client {
public:
  explicit Client(UnixSocket &&socket,
                  std::chrono::microseconds timeout = kDefaultSocketTimeout);

  ReplyMessage get_profiler_info();

private:
  UnixSocket _socket;
};

class Server {
public:
  explicit Server(UnixSocket &&socket,
                  std::chrono::microseconds timeout = kDefaultSocketTimeout);

  using ReplyFunc = std::function<ReplyMessage(const RequestMessage &)>;
  void waitForRequest(ReplyFunc func);

private:
  UnixSocket _socket;
};

} // namespace ddprof
