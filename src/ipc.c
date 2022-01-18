// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ipc.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

// TODO these two functions would benefit from sitting in a poll-timeout while
//      waiting on sfd, since that would cover the scenario where one player has
//      been killed (e.g., by a user)

bool sendfd(int sfd, int *fd, int sz) {
  // NB, kernel limit SCM_MAX_FD is the max number of file descriptors which can
  // be passed via this mechanism.  Should probably wrap this in a higher-level
  // abstraction for large-CPU machines
  //
  // NB although iov doesn't need to be used, we send the number of contained
  // file descriptors.  It's up to the caller to do something with it, possibly
  // validate the transmission or preallocate (although they can know this
  // some other way)
  struct msghdr *msg = &(struct msghdr){
      .msg_iov = &(struct iovec){.iov_base = &sz, .iov_len = sizeof(sz)},
      .msg_iovlen = 1,
      .msg_control = (char[CMSG_SPACE(DDPF_MAX_FD * sizeof(int))]){0},
      .msg_controllen = CMSG_SPACE(sz * sizeof(int))};
  CMSG_FIRSTHDR(msg)->cmsg_level = SOL_SOCKET;
  CMSG_FIRSTHDR(msg)->cmsg_type = SCM_RIGHTS;
  CMSG_FIRSTHDR(msg)->cmsg_len = CMSG_LEN(sz * sizeof(int));
  memcpy(CMSG_DATA(CMSG_FIRSTHDR(msg)), fd, sz * sizeof(int));
  if (sizeof(sz) != sendmsg(sfd, msg, MSG_NOSIGNAL))
    return false;

  return true;
}

// TODO right now this allocates memory which has to be freed by the caller,
//      maybe we can do a bit better with some refactoring.
int *getfd(int sfd, int *sz) {
  struct msghdr msg = {
      .msg_iov = &(struct iovec){.iov_base = &(int){0}, .iov_len = sizeof(int)},
      .msg_iovlen = 1,
      .msg_control = (char[CMSG_SPACE(DDPF_MAX_FD * sizeof(int))]){0},
      .msg_controllen = CMSG_SPACE(DDPF_MAX_FD * sizeof(int))};
  while (sizeof(int) != recvmsg(sfd, &msg, MSG_NOSIGNAL)) {
    if (errno != EINTR) {
      *sz = -1;
      return NULL;
    }
  }

  // Check, then copy in an alignment-safe way.
  *sz = *(int *)msg.msg_iov[0].iov_base;
  int num_fd = 0;
  struct cmsghdr *cmsg = NULL;
  int *fd = calloc(*sz, sizeof(*fd));
  if (!fd) {
    *sz = -3;
    return NULL;
  }
  for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      size_t len = cmsg->cmsg_len - CMSG_LEN(0); // subtract out container size
      memcpy(&fd[num_fd], CMSG_DATA(cmsg), len);
      num_fd += len / sizeof(int);
    }
  }

  if (num_fd > 0)
    return fd;
  free(fd);
  *sz = -2;
  return NULL;
}
