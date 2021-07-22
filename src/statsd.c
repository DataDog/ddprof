#include "statsd.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int statsd_open(char *path, size_t sz_path) {
  assert(path);
  assert(sz_path);

  char template[] = "/tmp/statsd.XXXXXX";
  struct sockaddr_un addr_bind = {.sun_family = AF_UNIX};
  struct sockaddr_un addr_peer = {.sun_family = AF_UNIX};
  int fd_tmp = -1;
  int fd_sock = -1;

  // We bind to a temporary file, since that's needed for the interface
  if (-1 == (fd_tmp = mkstemp(template))) {
    return -1;
  }
  unlink(template);
  if (-1 == (fd_sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0))) {
    close(fd_tmp);
    return -1;
  }
  memcpy(addr_bind.sun_path, template, sizeof(template) - 1);
  memcpy(addr_peer.sun_path, path, sz_path);
  if (bind(fd_sock, (struct sockaddr *)&addr_bind, sizeof(addr_bind))) {
    close(fd_tmp);
    close(fd_sock);
    return -1;
  }

  // Now connect to the other guy
  if (connect(fd_sock, (struct sockaddr *)&addr_peer, sizeof(addr_peer))) {
    close(fd_tmp);
    close(fd_sock);
    return -1;
  }

  // If we're here, then the connection has been fully established
  close(fd_tmp); // Pretty sure we don't need this anymore
  return fd_sock;
}

bool statsd_send(int fd_sock, char *key, void *val, int type) {
  char buf[1024] = {0};
  size_t sz = -1;
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
  if (sz <= 0 || sz >= sizeof(buf))
    return false;

  // Nothing to do if the write fails
  if (sz != (size_t)write(fd_sock, buf, sz))
    return false;
  return true;
}

bool statsd_close(int fd_sock) { return !close(fd_sock); }
