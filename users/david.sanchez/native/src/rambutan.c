#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/mman.h>

#include "perf.h"

int main(int argc, char** argv) {
  char filename[128] = {0};
  char* fp = strrchr("/"__FILE__, '/')+1;
  memcpy(filename, fp, strlen(fp));
  (strrchr(filename, '.'))[0] = 0;

  if(argc==1) {
    printf("%s is a tool for getting stack samples from an application.  Please wrap your application in it.\n", filename);
    return -1;
  }

  // Set the CPU affinity so that everything is on the same CPU.  Scream about
  // it because we want to undo this later..!
  cpu_set_t cpu_mask = {0};
  CPU_SET(0, &cpu_mask);
  if(!sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpu_mask)) {
    printf("Successfully set the CPU mask.\n");
  } else {
    printf("Failed to set the CPU mask.\n");
    return -1;
  }

  // Setup a shared barrier for timing
  pthread_barrierattr_t bat = {0};
  pthread_barrier_t *pb = mmap(NULL, sizeof(pthread_barrier_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

  pthread_barrierattr_init(&bat);
  pthread_barrierattr_setpshared(&bat, 1);
  pthread_barrier_init(pb, &bat, 2);

  // Fork, then run the child
  pid_t pid = fork();
  if(!pid) {
    pthread_barrier_wait(pb);
    munmap(pb, sizeof(pthread_barrier_t));
    execvp(argv[1], argv+1);
    printf("Hey, this shouldn't happen!\n");
    return -1;
  } else {
    PEvent pe = {0};
    char err =  perfopen(pid, &pe, NULL);
    if(-1 == err) {
      printf("Couldn't set up perf_event_open\n");
      return -1;
    }
    pthread_barrier_wait(pb);
    munmap(pb, sizeof(pthread_barrier_t));
    main_loop(&pe, NULL, NULL);
  }

  return 0;
}
