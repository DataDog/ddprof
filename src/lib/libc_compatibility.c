#include <sys/random.h>
#include <pthread.h>
#include <errno.h>
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>


int getentropy(void *buffer, size_t len)
{
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

    ret = syscall(SYS_getrandom, buffer, sizeof(buffer), 0);
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