// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <errno.h>
#include <pthread.h>
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <unistd.h>

int getentropy(void *buffer, size_t len) {
  int ret;
  char *pos = buffer;
  if (len > 256) {
    errno = EIO;
    return -1;
  }

  // libc implementation prevents cancels.
  // we should not depend on pthread, skipping this part
  //  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);

  while (len) {

    ret = syscall(SYS_getrandom, pos, sizeof(buffer), 0);
    if (ret < 0) {
      return -1;
    }

    pos += ret;
    len -= ret;
    ret = 0;
  }

  //  pthread_setcancelstate(cs, 0);

  return ret;
}
