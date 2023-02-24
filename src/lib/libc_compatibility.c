#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <unistd.h>

int getentropy(void *buffer, size_t len) {
  int ret = 0;
  char *pos = buffer;

  if (len > 256) {
    return -1;
  }

  // libc implementation prevents cancels.
  // we should not depend on pthread, skipping this part
  //  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);

  while (len) {
    ret = syscall(SYS_getrandom, pos, len, 0);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      else
        break;
    }
    pos += ret;
    len -= ret;
    ret = 0;
  }

  //  pthread_setcancelstate(cs, 0);

  return ret;
}
