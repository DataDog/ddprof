// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ipc.hpp"

#include "chrono_utils.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace ddprof {

namespace {
template <typename T> std::span<std::byte> to_byte_span(T *obj) {
  return {reinterpret_cast<std::byte *>(obj), sizeof(T)};
}

template <typename T> std::span<const std::byte> to_byte_span(const T *obj) {
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
} // namespace

void UnixSocket::close(std::error_code &ec) noexcept {
  error_wrapper(::close(_handle.release()), ec);
}

void UnixSocket::set_write_timeout(std::chrono::microseconds duration,
                                   std::error_code &ec) const noexcept {
  timeval tv = duration_to_timeval(duration);
  error_wrapper(
      ::setsockopt(_handle.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)),
      ec);
}

void UnixSocket::set_read_timeout(std::chrono::microseconds duration,
                                  std::error_code &ec) const noexcept {
  timeval tv = duration_to_timeval(duration);
  error_wrapper(
      ::setsockopt(_handle.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)),
      ec);
}

void UnixSocket::send(ConstBuffer buffer, std::error_code &ec) const noexcept {
  size_t written = 0;
  do {
    written += send_partial(remaining(buffer, written), ec);
  } while (!ec && written < buffer.size());
}

size_t UnixSocket::send_partial(ConstBuffer buffer,
                                std::error_code &ec) const noexcept {
  ssize_t ret;
  do {
    ret = ::send(_handle.get(), buffer.data(), buffer.size(), 0);
  } while (ret < 0 && errno == EINTR);

  error_wrapper(ret, ec);

  return ec ? 0 : ret;
}

void UnixSocket::send(ConstBuffer buffer, std::span<const int> fds,
                      std::error_code &ec) const noexcept {
  size_t const written = send_partial(buffer, fds, ec);

  if (!ec && written < buffer.size()) {
    send(remaining(buffer, written), ec);
  }
}

size_t UnixSocket::send_partial(ConstBuffer buffer, std::span<const int> fds,
                                std::error_code &ec) const noexcept {
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
    // interpreted as part of cmsg_len by glibc
    *cmsg = {};
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(payload_size);
    ::memcpy(CMSG_DATA(cmsg), fds.data(), payload_size);
  }

  ssize_t ret;
  do {
    ret = ::sendmsg(_handle.get(), &msg, 0);
  } while (ret < 0 && errno == EINTR);
  error_wrapper(ret, ec);

  return ec ? 0 : ret;
}

size_t UnixSocket::receive(std::span<std::byte> buffer,
                           std::error_code &ec) const noexcept {
  size_t read = 0;

  do {
    read += receive_partial(remaining(buffer, read), ec);
  } while (!ec && read < buffer.size());

  return read;
}

size_t UnixSocket::receive_partial(std::span<std::byte> buffer,
                                   std::error_code &ec) const noexcept {
  ssize_t ret;
  do {
    ret = ::recv(_handle.get(), buffer.data(), buffer.size(), 0);
  } while (ret < 0 && errno == EINTR);

  // 0 return means the other end has shutdown
  if (ret == 0) {
    ec = std::error_code(EAGAIN, std::system_category());
  } else {
    error_wrapper(ret, ec);
  }

  return ret < 0 ? 0 : ret;
}

std::pair<size_t, size_t>
UnixSocket::receive(std::span<std::byte> buffer, std::span<int> fds,
                    std::error_code &ec) const noexcept {
  auto [read, read_fds] = receive_partial(buffer, fds, ec);
  if (!ec && read < buffer.size()) {
    read += receive(remaining(buffer, read), ec);
  }
  return {read, read_fds};
}

std::pair<size_t, size_t>
UnixSocket::receive_partial(std::span<std::byte> buffer, std::span<int> fds,
                            std::error_code &ec) const noexcept {

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
    nr = ::recvmsg(_handle.get(), &msgh, 0);
  } while (nr < 0 && errno == EINTR);
  // 0 return means the other end has shutdown
  if (nr == 0) {
    ec = std::error_code(EAGAIN, std::system_category());
  } else {
    error_wrapper(nr, ec);
  }

  if (ec) {
    return {0, 0};
  }

  cmsghdr *cmsgp = CMSG_FIRSTHDR(&msgh);

  if (cmsgp == nullptr) {
    return {nr, 0};
  }

  int const nfds =
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

DDRes send(const UnixSocket &socket, const RequestMessage &msg) {
  std::error_code ec;
  socket.send(to_byte_span(&msg), ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET, "Unable to send request message");
  return {};
}

DDRes send(const UnixSocket &socket, const ReplyMessage &msg) {
  int fds[2] = {msg.ring_buffer.ring_fd, msg.ring_buffer.event_fd};
  std::span<int> const fd_span{fds,
                               (msg.ring_buffer.mem_size != -1) ? 2UL : 0UL};
  std::error_code ec;
  socket.send(to_byte_span(&msg), fd_span, ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET, "Unable to send response message");
  return {};
}

DDRes receive(const UnixSocket &socket, RequestMessage &msg) {
  std::error_code ec;
  socket.receive(to_byte_span(&msg), ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET,
                        "Unable to receive request message");

  return {};
}

DDRes receive(const UnixSocket &socket, ReplyMessage &msg) {
  int fds[2] = {-1, -1};
  std::error_code ec;
  auto res = socket.receive(to_byte_span(&msg), fds, ec);

  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET,
                        "Unable to receive response message");
  if ((msg.ring_buffer.mem_size != -1) ^ (res.second == 2)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_SOCKET,
                           "Unable to receive response message");
  }
  msg.ring_buffer.ring_fd = fds[0];
  msg.ring_buffer.event_fd = fds[1];
  return {};
}

DDRes get_profiler_info(UniqueFd &&client_socket,
                        std::chrono::microseconds timeout,
                        ReplyMessage *reply) noexcept {
  UnixSocket const socket{std::move(client_socket)};
  std::error_code ec;
  socket.set_read_timeout(timeout, ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET,
                        "Unable to set read timeout on socket");
  socket.set_write_timeout(timeout, ec);
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_SOCKET,
                        "Unable to set write timeout on socket");

  RequestMessage const request = {.request = RequestMessage::kProfilerInfo,
                                  .pid = getpid()};
  DDRES_CHECK_FWD(send(socket, request));
  DDRES_CHECK_FWD(receive(socket, *reply));
  return {};
}

UniqueFd create_server_socket(std::string_view socket_path) noexcept {
  UniqueFd fd{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)};
  if (fd.get() < 0) {
    LG_ERR("Unable to create server socket: %s",
           std::error_code(errno, std::system_category()).message().c_str());
    return {};
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (socket_path.size() >= sizeof(addr.sun_path)) {
    LG_ERR("Unable to bind socket, socket path too long: %.*s",
           static_cast<int>(socket_path.size()), socket_path.data());
    return {};
  }
  std::copy(socket_path.begin(), socket_path.end(), addr.sun_path);
  if (is_socket_abstract(socket_path)) {
    addr.sun_path[0] = '\0';
  }

  if (::bind(fd.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    LG_ERR("Unable to bind server socket: %s",
           std::error_code(errno, std::system_category()).message().c_str());
    return {};
  }

  constexpr auto kMaxConn = 128;
  if (::listen(fd.get(), kMaxConn) < 0) {
    LG_ERR("Unable to listen to server socket: %s",
           std::error_code(errno, std::system_category()).message().c_str());
    return {};
  }

  return fd;
}

UniqueFd create_client_socket(std::string_view socket_path) noexcept {
  UniqueFd fd{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)};
  if (fd.get() < 0) {
    LG_ERR("Unable to create client socket: %s",
           std::error_code(errno, std::system_category()).message().c_str());
    return {};
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (socket_path.size() >= sizeof(addr.sun_path)) {
    LG_ERR("Unable to connect to socket, socket path too long: %.*s",
           static_cast<int>(socket_path.size()), socket_path.data());
    return {};
  }
  std::copy(socket_path.begin(), socket_path.end(), addr.sun_path);
  if (is_socket_abstract(socket_path)) {
    addr.sun_path[0] = '\0';
  }

  if (::connect(fd.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) <
      0) {
    LG_ERR("Unable to connect to socket: %s",
           std::error_code(errno, std::system_category()).message().c_str());
    return {};
  }

  return fd;
}

WorkerServer::WorkerServer(int socket, const ReplyMessage &msg)
    : _socket(socket), _latch(1), _msg(msg),
      _loop_thread(&WorkerServer::event_loop, this) {
  // wait for loop thread to be ready
  _latch.wait();
}

WorkerServer::~WorkerServer() {
  pthread_kill(_loop_thread.native_handle(), SIGUSR1);
  // _loop_thread destructor will join the thread
}

WorkerServer start_worker_server(int socket, const ReplyMessage &msg) {
  return WorkerServer{socket, msg};
}

void WorkerServer::event_loop() {
  // block SIGUSR1
  sigset_t mask;
  ::sigemptyset(&mask);
  ::sigaddset(&mask, SIGUSR1);
  ::sigprocmask(SIG_BLOCK, &mask, nullptr);
  int const sfd = ::signalfd(-1, &mask, 0);

  // signal launch thread that we are ready
  _latch.count_down();

  std::vector<pollfd> poll_fds;
  poll_fds.push_back({.fd = _socket, .events = POLLIN});
  poll_fds.push_back({.fd = sfd, .events = POLLIN});

  while (true) {
    int const ret = poll(poll_fds.data(), poll_fds.size(), -1);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    if (poll_fds[1].revents & POLLIN) {
      break;
    }
    auto first = poll_fds.begin() + 2;
    auto last = poll_fds.end();
    while (first != last) {
      if (first->revents) {
        UnixSocket const sock(first->fd);
        if (first->revents & POLLIN) {
          RequestMessage request;
          if (IsDDResOK(receive(sock, request))) {
            LG_DBG("Received request from pid: %d", request.pid);
            send(sock, _msg);
          }
        }
        last = std::prev(last);
        std::swap(*first, *last);
      } else {
        ++first;
      }
    }
    poll_fds.erase(last, poll_fds.end());
    if (poll_fds[0].revents & POLLIN) {
      int const new_socket = ::accept(poll_fds[0].fd, nullptr, nullptr);
      if (new_socket != -1) {
        auto tv = duration_to_timeval(kDefaultSocketTimeout);
        ::setsockopt(new_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(new_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        poll_fds.push_back({.fd = new_socket, .events = POLLIN});
      }
    }
  }

  // close all remaining connections
  for_each(poll_fds.begin() + 1, poll_fds.end(),
           [](pollfd &fd) { ::close(fd.fd); });
}

bool is_socket_abstract(std::string_view path) noexcept {
  // interpret @ as first character as request for an abstract socket
  return path.starts_with("@");
}

} // namespace ddprof
