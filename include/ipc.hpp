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
#include <latch>
#include <span>
#include <system_error>
#include <thread>

namespace ddprof {

// Maximum number of file descriptors that can be transferred with
// sendmsg/recvmsg. Taken from default value for SCM_MAX_FD, which is a kernel
// configuration. 253 is a common safe lower bound for that value.
static constexpr size_t kMaxFD = 253;

static constexpr auto kDefaultSocketTimeout = std::chrono::seconds{2};

class UnixSocket {
public:
  using socket_t = int;

  explicit UnixSocket(socket_t handle) noexcept : _handle(handle) {}
  explicit UnixSocket(UniqueFd &&handle) noexcept
      : _handle(std::move(handle)) {}

  void close(std::error_code &ec) noexcept;

  void set_write_timeout(std::chrono::microseconds duration,
                         std::error_code &ec) const noexcept;
  void set_read_timeout(std::chrono::microseconds duration,
                        std::error_code &ec) const noexcept;

  void send(ConstBuffer buffer, std::error_code &ec) const noexcept;
  size_t send_partial(ConstBuffer buffer, std::error_code &ec) const noexcept;
  void send(ConstBuffer buffer, std::span<const int> fds,
            std::error_code &ec) const noexcept;
  size_t send_partial(ConstBuffer buffer, std::span<const int> fds,
                      std::error_code &ec) const noexcept;

  std::pair<size_t, size_t> receive(Buffer buffer, std::span<int> fds,
                                    std::error_code &ec) const noexcept;

  std::pair<size_t, size_t> receive_partial(Buffer buffer, std::span<int> fds,
                                            std::error_code &ec) const noexcept;

  size_t receive(Buffer buffer, std::error_code &ec) const noexcept;
  size_t receive_partial(Buffer buffer, std::error_code &ec) const noexcept;

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
  pid_t pid = -1;
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

DDRes send(const UnixSocket &socket, const RequestMessage &msg);
DDRes send(const UnixSocket &socket, const ReplyMessage &msg);
DDRes receive(const UnixSocket &socket, RequestMessage &msg);
DDRes receive(const UnixSocket &socket, ReplyMessage &msg);

UniqueFd create_server_socket(std::string_view path) noexcept;
UniqueFd create_client_socket(std::string_view path) noexcept;
DDRes get_profiler_info(UniqueFd &&socket, std::chrono::microseconds timeout,
                        ReplyMessage *reply) noexcept;

bool is_socket_abstract(std::string_view path) noexcept;

class WorkerServer {
public:
  WorkerServer(const WorkerServer &) = delete;
  WorkerServer &operator=(const WorkerServer &) = delete;
  ~WorkerServer();

private:
  friend WorkerServer start_worker_server(int socket, const ReplyMessage &msg);

  WorkerServer(int socket, const ReplyMessage &msg);
  void event_loop();

  int _socket;
  std::latch _latch;
  ReplyMessage _msg;
  std::jthread _loop_thread;
};

WorkerServer start_worker_server(int socket, const ReplyMessage &msg);

} // namespace ddprof
