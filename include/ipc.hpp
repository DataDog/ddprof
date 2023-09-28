// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_buffer.hpp"
#include "ddres.hpp"
#include "unique_fd.hpp"

#include <chrono>
#include <functional>
#include <span>
#include <system_error>

namespace ddprof {

// Maximum number of file descriptors that can be transferred with
// sendmsg/recvmsg. Taken from default value for SCM_MAX_FD, which is a kernel
// configuration. 253 is a common safe lower bound for that value.
static constexpr size_t kMaxFD = 253;

static constexpr auto kDefaultSocketTimeout = std::chrono::seconds{2};

class UnixSocket {
public:
  using socket_t = int;
  UnixSocket() noexcept = default;

  explicit UnixSocket(socket_t handle) noexcept : _handle(handle) {}

  UnixSocket(UnixSocket &&socket) noexcept = default;
  UnixSocket(const UnixSocket &) = delete;

  UnixSocket &operator=(const UnixSocket &) = delete;
  UnixSocket &operator=(UnixSocket &&socket) noexcept = default;

  void close(std::error_code &ec) noexcept;

  void set_write_timeout(std::chrono::microseconds duration,
                         std::error_code &ec) const noexcept;
  void set_read_timeout(std::chrono::microseconds duration,
                        std::error_code &ec) const noexcept;

  void send(ConstBuffer buffer, std::error_code &ec) noexcept;
  size_t send_partial(ConstBuffer buffer, std::error_code &ec) noexcept;
  void send(ConstBuffer buffer, std::span<const int> fds,
            std::error_code &ec) noexcept;
  size_t send_partial(ConstBuffer buffer, std::span<const int> fds,
                      std::error_code &ec) noexcept;

  std::pair<size_t, size_t> receive(Buffer buffer, std::span<int> fds,
                                    std::error_code &ec) noexcept;

  std::pair<size_t, size_t> receive_partial(Buffer buffer, std::span<int> fds,
                                            std::error_code &ec) noexcept;

  size_t receive(Buffer buffer, std::error_code &ec) noexcept;
  size_t receive_partial(Buffer buffer, std::error_code &ec) noexcept;

  // cppcheck thinks we are returning an address here, but is completely wrong
  // cppcheck-suppress CastAddressToIntegerAtReturn
  socket_t release() noexcept { return _handle.release(); }

private:
  UniqueFd _handle;
};

struct RequestMessage {
  // Request flags
  enum { kProfilerInfo = 0x1 };
  // request is bit mask of request flags
  uint32_t request = 0;
};

struct RingBufferInfo {
  int64_t mem_size = -1;
  int32_t ring_fd = -1;
  int32_t event_fd = -1;
  int32_t ring_buffer_type = 0;
};

struct ReplyMessage {
  enum { kLiveCallgraph = 0 };
  // reply with the request flags from the request
  uint32_t request = 0;
  // profiler pid
  int32_t pid = -1;
  int64_t allocation_profiling_rate = 0;
  // RingBufferInfo is returned if request & kRingBuffer
  // cppcheck-suppress unusedStructMember
  RingBufferInfo ring_buffer;
  uint32_t initial_loaded_libs_check_delay_ms = 0;
  uint32_t loaded_libs_check_interval_ms = 0;
  uint32_t allocation_flags = 0;
  uint32_t stack_sample_size = 0;
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
