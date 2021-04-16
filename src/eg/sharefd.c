#include "perf.h"

int main(void) {
  int sfd[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sfd)) {
    printf("Error creating socket pair\n");
  }

  int pid = fork();
  if (pid) {
    int fd = open("/tmp/foo", O_RDWR | O_CREAT, 0777);
    if (sendfd(sfd[1], fd)) {
      printf("Error sending.\n");
    }
  } else {
    int fd = getfd(sfd[0]);
    if (-1 == write(fd, "HI", 2)) {
      printf("Error writing?\n");
    }
  }
  return 0;
}
