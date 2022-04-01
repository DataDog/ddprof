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

namespace ddprof {

template <typename T> ddprof::span<std::byte> to_byte_span(T *obj) {
  return {reinterpret_cast<std::byte *>(obj), sizeof(T)};
}

template <typename T> ddprof::span<const std::byte> to_byte_span(const T *obj) {
  return {reinterpret_cast<const std::byte *>(obj), sizeof(T)};
}

ssize_t send(int sfd, ddprof::span<const std::byte> buffer,
             ddprof::span<const int> fds) {
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
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(payload_size);
    memcpy(CMSG_DATA(cmsg), fds.data(), payload_size);
  }

  if (sendmsg(sfd, &msg, 0) != static_cast<ssize_t>(buffer.size())) {
    return -1;
  }

  return 0;
}

std::pair<ssize_t, size_t> receive(int sockfd, ddprof::span<std::byte> buffer,
                                   ddprof::span<int> fds) {

  msghdr msgh;

  /* Allocate a char buffer for the ancillary data. See the comments
     in sendfd() */
  union {
    char buf[CMSG_SPACE(kMaxFD * sizeof(int))];
    cmsghdr align;
  } controlMsg;

  /* The 'msg_name' field can be used to obtain the address of the
     sending socket. However, we do not need this information. */

  msgh.msg_name = NULL;
  msgh.msg_namelen = 0;

  /* Specify buffer for receiving real data */
  iovec iov = {.iov_base = buffer.data(), .iov_len = buffer.size()};
  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  iov.iov_base = buffer.data();
  iov.iov_len = buffer.size();

  /* Set 'msghdr' fields that describe ancillary data */
  msgh.msg_control = controlMsg.buf;
  msgh.msg_controllen = sizeof(controlMsg.buf);

  ssize_t nr = recvmsg(sockfd, &msgh, 0);
  if (nr == -1) {
    return {-1, 0};
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
    return {-1, 0};
  }

  memcpy(fds.data(), CMSG_DATA(cmsgp), nfds * sizeof(int));
  return {nr, nfds};
}

bool send(int sfd, const RequestMessage &msg) {
  return send(sfd, to_byte_span(&msg)) >= 0;
}

bool send(int sfd, const ResponseMessage &msg) {
  int fds[2] = {msg.fds.mem_fd, msg.fds.event_fd};
  ddprof::span<int> fd_span{
      fds, (msg.data.request & RequestMessage::kRingBuffer) ? 2ul : 0ul};
  return send(sfd, to_byte_span(&msg.data), fd_span) >= 0;
}

bool receive(int sfd, RequestMessage &msg) {
  auto res = receive(sfd, to_byte_span(&msg));
  return res.first == sizeof(msg) && res.second == 0;
}

bool receive(int sfd, ResponseMessage &msg) {
  int fds[2];
  auto res = receive(sfd, to_byte_span(&msg.data), fds);
  if (res.first != sizeof(msg.data) ||
      ((msg.data.request & RequestMessage::kRingBuffer) ^ (res.second == 2))) {
    return false;
  }
  msg.fds.mem_fd = fds[0];
  msg.fds.event_fd = fds[1];
  return true;
}

} // namespace ddprof
