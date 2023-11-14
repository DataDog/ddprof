// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "statsd.hpp"

#include "ddres.hpp"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ddprof {

namespace {
std::string_view adjust_uds_path(std::string_view path) {
  // Check if the socket_path starts with unix://
  static constexpr std::string_view prefix{"unix://"};
  if (path.starts_with(prefix)) {
    path.remove_prefix(prefix.length());
  }
  return path;
}
} // namespace

DDRes statsd_listen(std::string_view path, int *fd) {
  struct sockaddr_un addr_bind = {.sun_family = AF_UNIX};
  path = adjust_uds_path(path);
  // Open the socket
  if (path.size() >= sizeof(addr_bind.sun_path) - 1) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "[STATSD] %.*s path is too long",
                          static_cast<int>(path.size()), path.data());
  }
  strncpy(addr_bind.sun_path, path.data(), path.size());
  addr_bind.sun_path[path.size()] = '\0';
  const int socktype = SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
  const int fd_sock = socket(AF_UNIX, socktype, 0);
  if (-1 == fd_sock) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "[STATSD] Creating UDS failed (%s)",
                          strerror(errno));
  }

  // Attempt to bind to the given path.  This is necessary for datagram-type
  // Unix domain sockets, since like UDP connections both the client and the
  // server need to be reachable on some resource.  TCP (and streaming UDS)
  // get that client-reachability satisfied at the protocol level, via the
  // assignment of ephemeral ports.  But for datagram-UDS/UDP we have to do
  // it ourselves.
  // In particular, that means if the user does not have the permission to
  // create and use a node somewhere on the VFS, then they cannot open a
  // listen-type datagram UDS.
  // TODO: is this true?  Can we relax this constraint?
  if (bind(fd_sock, reinterpret_cast<struct sockaddr *>(&addr_bind),
           sizeof(addr_bind))) {
    close(fd_sock);
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Binding UDS failed (%s)",
                          strerror(errno));
  }

  *fd = fd_sock;
  return {};
}

DDRes statsd_connect(std::string_view statsd_socket, int *fd) {
  assert(!statsd_socket.empty());
  assert(fd);

  char path_listen[] = "/tmp/" MYNAME ".1234567890";
  size_t sz = 0;
  sz =
      snprintf(path_listen, sizeof(path_listen), "/tmp/" MYNAME "%d", getpid());
  struct sockaddr_un addr_peer = {.sun_family = AF_UNIX};
  int fd_sock = -1;

  statsd_socket = adjust_uds_path(statsd_socket);
  if (statsd_socket.size() >= sizeof(addr_peer.sun_path) - 1) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "[STATSD] %.*s path is too long",
                          static_cast<int>(statsd_socket.size()),
                          statsd_socket.data());
  }
  strncpy(addr_peer.sun_path, statsd_socket.data(), statsd_socket.size());
  addr_peer.sun_path[statsd_socket.size()] = '\0';
  DDRes res = statsd_listen(std::string_view(path_listen, sz), &fd_sock);
  unlink(path_listen);
  if (IsDDResNotOK(res)) {
    return res;
  }

  // Now connect to the specified listening path
  if (connect(fd_sock, reinterpret_cast<struct sockaddr *>(&addr_peer),
              sizeof(addr_peer))) {
    close(fd_sock);
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD,
                          "[STATSD] Connecting to host failed (%s)",
                          strerror(errno));
  }

  // If we're here, then the connection has been fully established
  *fd = fd_sock;
  return {};
}

DDRes statsd_send(int fd_sock, const char *key, const void *val, int type) {
  constexpr size_t k_max_msg_length = 1024;
  char buf[k_max_msg_length] = {0};
  size_t sz = 0;
  switch (type) {
  default:
  case STAT_MS_LONG:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key,
                  *reinterpret_cast<const long *>(val), "ms");
    break;
  case STAT_MS_FLOAT:
    sz = snprintf(buf, sizeof(buf), "%s:%f|%s", key,
                  *reinterpret_cast<const float *>(val), "ms");
    break;
  case STAT_COUNT:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key,
                  *reinterpret_cast<const long *>(val), "c");
    break;
  case STAT_GAUGE:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key,
                  *reinterpret_cast<const long *>(val), "g");
    break;
  }

  // Nothing to do if serialization failed or was short, but we don't return
  // granular result
  if (sz == 0 || sz >= sizeof(buf)) {
    // Not fatal
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Serialization failed");
  }

  // Nothing to do if the write fails
  while (static_cast<ssize_t>(sz) != write(fd_sock, buf, sz) &&
         errno == EINTR) {
    // Don't consider this as fatal.
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Write failed (sys buffer full)");
    } else {
      DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Write failed");
    }
  }
  return {};
}

DDRes statsd_close(int fd_sock) {
  DDRES_CHECK_INT(close(fd_sock), DD_WHAT_STATSD, "Error while closing socket");
  return {};
}

} // namespace ddprof
