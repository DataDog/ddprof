#include "statsd.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ddres.h"

DDRes statsd_listen(const char *path, size_t sz_path, int *fd) {
  struct sockaddr_un addr_bind = {.sun_family = AF_UNIX};
  int fd_sock = -1;

  // Open the socket
  memcpy(addr_bind.sun_path, path, sz_path);
  if (-1 == (fd_sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0))) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Creating UDS failed (%s)",
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
  if (bind(fd_sock, (struct sockaddr *)&addr_bind, sizeof(addr_bind))) {
    close(fd_sock);
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Binding UDS failed (%s)",
                          strerror(errno));
  }

  *fd = fd_sock;
  return ddres_init();
}

DDRes statsd_connect(const char *path, size_t sz_path, int *fd) {
  assert(path);
  assert(sz_path);
  assert(fd);

  char template[] = "/tmp/statsd.XXXXXX";
  struct sockaddr_un addr_peer = {.sun_family = AF_UNIX};
  int fd_sock = -1;
  int fd_tmp = -1;

  // We bind to a temporary file, since that's needed for the interface
  if (-1 == (fd_tmp = mkstemp(template))) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Creating temporary file failed (%s)",
                          strerror(errno));
  }

  unlink(template);
  memcpy(addr_peer.sun_path, path, sz_path);
  DDRes res = statsd_listen(template, sizeof(template) - 1, &fd_sock);

  if (IsDDResNotOK(res)) {
    close(fd_tmp);
    return res;
  }

  // Now connect to the specified listening path
  if (connect(fd_sock, (struct sockaddr *)&addr_peer, sizeof(addr_peer))) {
    close(fd_tmp);
    close(fd_sock);
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Connecting to host failed (%s)",
                          strerror(errno));
  }

  // If we're here, then the connection has been fully established
  close(fd_tmp); // The listen file descriptor is no longer needed
  *fd = fd_sock;
  return ddres_init();
}

DDRes statsd_send(int fd_sock, const char *key, void *val, int type) {
  char buf[1024] = {0};
  size_t sz = 0;
  switch (type) {
  default:
  case STAT_MS_LONG:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key, *(long *)val, "ms");
    break;
  case STAT_MS_FLOAT:
    sz = snprintf(buf, sizeof(buf), "%s:%f|%s", key, *(float *)val, "ms");
    break;
  case STAT_COUNT:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key, *(long *)val, "c");
    break;
  case STAT_GAUGE:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key, *(long *)val, "g");
    break;
  }

  // Nothing to do if serialization failed or was short, but we don't return
  // granular result
  if (sz == 0 || sz >= sizeof(buf)) {
    // Not fatal
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Serialization failed");
  }

  // Nothing to do if the write fails
  if (sz != (size_t)write(fd_sock, buf, sz)) {
    // Don't consider this as fatal.
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Write failed");
  }
  return ddres_init();
}

DDRes statsd_close(int fd_sock) {
  DDRES_CHECK_INT(close(fd_sock), DD_WHAT_STATSD, "Error while closing socket");
  return ddres_init();
}
