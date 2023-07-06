// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ipc.hpp"

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace ddprof {

namespace {
// Internal structure for response that is sent over the wire
struct InternalResponseMessage {
  uint32_t request;
  int32_t pid;
  int64_t mem_size;
  int64_t allocation_profiling_rate;
  uint32_t stack_sample_size;
  int32_t ring_buffer_type;
  int32_t allocation_flags;
};

struct timeval to_timeval(std::chrono::microseconds duration) noexcept {
  const std::chrono::seconds sec =
      std::chrono::duration_cast<std::chrono::seconds>(duration);

  struct timeval tv;
  tv.tv_sec = time_t(sec.count());
  tv.tv_usec = suseconds_t(
      std::chrono::duration_cast<std::chrono::microseconds>(duration - sec)
          .count());
  return tv;
}

} // namespace

template <typename T> ddprof::span<std::byte> to_byte_span(T *obj) {
  return {reinterpret_cast<std::byte *>(obj), sizeof(T)};
}

template <typename T> ddprof::span<const std::byte> to_byte_span(const T *obj) {
  return {reinterpret_cast<const std::byte *>(obj), sizeof(T)};
}

template <typename ReturnType>
inline ReturnType error_wrapper(ReturnType return_value,
                                std::error_code &ec) noexcept {
  if (return_value < 0) {
    ec = std::error_code(errno, std::system_category());
  } else {
    ec = std::error_code();
  }
  return return_value;
}

UnixSocket::~UnixSocket() {
  if (_handle != kInvalidSocket) {
    ::close(_handle);
  }
}

void UnixSocket::close(std::error_code &ec) noexcept {
  error_wrapper(::close(_handle), ec);
}

void UnixSocket::set_write_timeout(std::chrono::microseconds duration,
                                   std::error_code &ec) noexcept {
  timeval tv = to_timeval(duration);
  error_wrapper(::setsockopt(_handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)),
                ec);
}

void UnixSocket::set_read_timeout(std::chrono::microseconds duration,
                                  std::error_code &ec) noexcept {
  timeval tv = to_timeval(duration);
  error_wrapper(::setsockopt(_handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)),
                ec);
}

void UnixSocket::send(ConstBuffer buffer, std::error_code &ec) noexcept {
  size_t written = 0;
  do {
    written += send_partial(remaining(buffer, written), ec);
  } while (!ec && written < buffer.size());
}

size_t UnixSocket::send_partial(ConstBuffer buffer,
                                std::error_code &ec) noexcept {
  ssize_t ret;
  do {
    ret = ::send(_handle, buffer.data(), buffer.size(), 0);
  } while (ret < 0 && errno == EINTR);

  error_wrapper(ret, ec);

  return ec ? 0 : ret;
}

void UnixSocket::send(ConstBuffer buffer, ddprof::span<const int> fds,
                      std::error_code &ec) noexcept {
  size_t written = send_partial(buffer, fds, ec);

  if (!ec && written < buffer.size()) {
    send(remaining(buffer, written), ec);
  }
}

size_t UnixSocket::send_partial(ConstBuffer buffer, ddprof::span<const int> fds,
                                std::error_code &ec) noexcept {
  msghdr msg = {};
  if (fds.size() > kMaxFD || buffer.empty()) {
    return -1;
  }

  iovec io = {.iov_base = const_cast<std::byte *>(buffer.data()),
              .iov_len = buffer.size()};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;

  union { /* Ancillary data buffer, wrapped in a union
             in order to ensure it is suitably aligned */
    char buf[CMSG_SPACE(kMaxFD * sizeof(int))];
    cmsghdr align;
  } u;

  if (!fds.empty()) {
    const size_t payload_size = fds.size() * sizeof(int);
    msg.msg_control = u.buf;
    msg.msg_controllen = CMSG_SPACE(payload_size);
    cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    // musl use a smaller type for cmsg_len with extra padding
    // if this padding is not initialized to zero, if will be wrongly
    // interpreted as part of cmsg_len by glic
    *cmsg = {};
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(payload_size);
    ::memcpy(CMSG_DATA(cmsg), fds.data(), payload_size);
  }

  ssize_t ret;
  do {
    ret = ::sendmsg(_handle, &msg, 0);
  } while (ret < 0 && errno == EINTR);
  error_wrapper(ret, ec);

  return ec ? 0 : ret;
}

size_t UnixSocket::receive(ddprof::span<std::byte> buffer,
                           std::error_code &ec) noexcept {
  size_t read = 0;

  do {
    read += receive_partial(remaining(buffer, read), ec);
  } while (!ec && read < buffer.size());

  return read;
}

size_t UnixSocket::receive_partial(ddprof::span<std::byte> buffer,
                                   std::error_code &ec) noexcept {
  ssize_t ret;
  do {
    ret = ::recv(_handle, buffer.data(), buffer.size(), 0);
  } while (ret < 0 && errno == EINTR);
  error_wrapper(ret, ec);

  return ret < 0 ? 0 : ret;
}

std::pair<size_t, size_t> UnixSocket::receive(ddprof::span<std::byte> buffer,
                                              ddprof::span<int> fds,
                                              std::error_code &ec) noexcept {
  auto [read, read_fds] = receive_partial(buffer, fds, ec);
  if (!ec && read < buffer.size()) {
    read += receive(remaining(buffer, read), ec);
  }
  return {read, read_fds};
}

std::pair<size_t, size_t>
UnixSocket::receive_partial(ddprof::span<std::byte> buffer,
                            ddprof::span<int> fds,
                            std::error_code &ec) noexcept {

  msghdr msgh = {};

  union {
    /* Ancillary data buffer, wrapped in a union
       in order to ensure it is suitably aligned */
    char buf[CMSG_SPACE(kMaxFD * sizeof(int))];
    cmsghdr align;
  } controlMsg;

  /* Specify buffer for receiving real data */
  iovec iov = {.iov_base = buffer.data(), .iov_len = buffer.size()};
  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  iov.iov_base = buffer.data();
  iov.iov_len = buffer.size();

  /* Set 'msghdr' fields that describe ancillary data */
  msgh.msg_control = controlMsg.buf;
  msgh.msg_controllen = sizeof(controlMsg.buf);

  ssize_t nr;
  do {
    nr = ::recvmsg(_handle, &msgh, 0);
  } while (nr < 0 && errno == EINTR);
  error_wrapper(nr, ec);
  if (ec) {
    return {0, 0};
  }

  cmsghdr *cmsgp = CMSG_FIRSTHDR(&msgh);

  if (cmsgp == NULL) {
    return {nr, 0};
  }

  int nfds =
      (cmsgp->cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) / sizeof(int);
  if (nfds == 0) {
    return {nr, 0};
  }

  /* Check the validity of the 'cmsghdr' */
  if (static_cast<size_t>(nfds) > fds.size() ||
      cmsgp->cmsg_level != SOL_SOCKET || cmsgp->cmsg_type != SCM_RIGHTS) {
    return {nr, 0};
  }

  ::memcpy(fds.data(), CMSG_DATA(cmsgp), nfds * sizeof(int));
  return {nr, nfds};
}

DDRes send(UnixSocket &socket, const RequestMessage &msg) {
  std::error_code ec;
  socket.send(to_byte_span(&msg), ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET, "Unable to send request message");
  return {};
}

DDRes send(UnixSocket &socket, const ReplyMessage &msg) {
  int fds[2] = {msg.ring_buffer.ring_fd, msg.ring_buffer.event_fd};
  ddprof::span<int> fd_span{fds, (msg.ring_buffer.mem_size != -1) ? 2ul : 0ul};
  std::error_code ec;
  InternalResponseMessage data = {
      .request = msg.request,
      .pid = msg.pid,
      .mem_size = msg.ring_buffer.mem_size,
      .allocation_profiling_rate = msg.allocation_profiling_rate,
      .stack_sample_size = msg.stack_sample_size,
      .ring_buffer_type = msg.ring_buffer.ring_buffer_type,
      .allocation_flags = msg.allocation_flags};
  socket.send(to_byte_span(&data), fd_span, ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET, "Unable to send response message");
  return {};
}

DDRes receive(UnixSocket &socket, RequestMessage &msg) {
  std::error_code ec;
  socket.receive(to_byte_span(&msg), ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET,
                        "Unable to receive request message");

  return {};
}

DDRes receive(UnixSocket &socket, ReplyMessage &msg) {
  int fds[2] = {-1, -1};
  std::error_code ec;
  InternalResponseMessage data;
  auto res = socket.receive(to_byte_span(&data), fds, ec);

  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET,
                        "Unable to receive response message");
  if ((data.mem_size != -1) ^ (res.second == 2)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_SOCKET,
                           "Unable to receive response message");
  }
  msg.pid = data.pid;
  msg.request = data.request;
  msg.allocation_profiling_rate = data.allocation_profiling_rate;
  msg.stack_sample_size = data.stack_sample_size;
  msg.ring_buffer.mem_size = data.mem_size;
  msg.ring_buffer.ring_buffer_type = data.ring_buffer_type;
  msg.ring_buffer.ring_fd = fds[0];
  msg.ring_buffer.event_fd = fds[1];
  msg.allocation_flags = data.allocation_flags;
  return {};
}

Client::Client(UnixSocket &&socket, std::chrono::microseconds timeout)
    : _socket(std::move(socket)) {
  std::error_code ec;
  _socket.set_read_timeout(timeout, ec);
  if (ec) {
    DDRES_THROW_EXCEPTION(DD_WHAT_SOCKET, "Unable to set timeout on socket");
  }
  _socket.set_write_timeout(timeout, ec);
  if (ec) {
    DDRES_THROW_EXCEPTION(DD_WHAT_SOCKET, "Unable to set timeout on socket");
  }
}

ReplyMessage Client::get_profiler_info() {
  RequestMessage request = {.request = RequestMessage::kProfilerInfo};
  DDRES_CHECK_THROW_EXCEPTION(send(_socket, request));
  ReplyMessage reply;
  DDRES_CHECK_THROW_EXCEPTION(ddprof::receive(_socket, reply));
  return reply;
}

Server::Server(UnixSocket &&socket, std::chrono::microseconds timeout)
    : _socket(std::move(socket)) {
  std::error_code ec;
  _socket.set_read_timeout(timeout, ec);
  if (ec) {
    DDRES_THROW_EXCEPTION(DD_WHAT_SOCKET, "Unable to set timeout on socket");
  }
  _socket.set_write_timeout(timeout, ec);
  if (ec) {
    DDRES_THROW_EXCEPTION(DD_WHAT_SOCKET, "Unable to set timeout on socket");
  }
}

void Server::waitForRequest(ReplyFunc func) {
  RequestMessage request;
  DDRES_CHECK_THROW_EXCEPTION(ddprof::receive(_socket, request));
  ReplyMessage reply = func(request);
  DDRES_CHECK_THROW_EXCEPTION(ddprof::send(_socket, reply));
}

} // namespace ddprof
