#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <x86intrin.h>

#include "statsd.h"

#ifdef MYNAME
#undef MYNAME
#define MYNAME "collatz"
#endif
#define VER_MAJ 1
#define VER_MIN 4
#define VER_PATCH 0
#ifndef VER_REV
#  define VER_REV "custom"
#endif

unsigned long* counter = NULL;
unsigned long* my_counter = &(unsigned long){0};

// Helper for processing input
#define P(s, d) ({                \
  long _t = strtoll(s, NULL, 10); \
  if (_t) d = _t;                 \
})

// This is the function body for every expanded function in the X-table
#define FUNBOD {                          \
  int n = x&1 ? x*3+1 : x/2;              \
  __sync_add_and_fetch(my_counter, 1);    \
  return 1 >= n ? 1 : funs[n%funlen](n);  \
}

// Macro system for recursively expanding concatenated names into an
// X-table
#define N1(X,f) X(f)
#define N2(X,f) N1(X,f##0) N1(X,f##1) N1(X,f##2) N1(X,f##3) N1(X,f##4) N1(X,f##5) N1(X,f##6) N1(X,f##7) N1(X,f##8) N1(X,f##9)
#define N3(X,f) N2(X,f##0) N2(X,f##1) N2(X,f##2) N2(X,f##3) N2(X,f##4) N2(X,f##5) N2(X,f##6) N2(X,f##7) N2(X,f##8) N2(X,f##9)
#define C(X,N) N(X,f0) N(X,f1) N(X,f2) N(X,f3) N(X,f4) N(X,f5) N(X,f6) N(X,f7) N(X,f8) N(X,f9)

// X-table; use something like gcc -E collatz.c to see how this works :)
#define COLLATZ(X) C(X,N3)
#define DECL(f) int f(int);
#define NAME(f) f,
#define DEFN(f) int f(int x) FUNBOD;

// Declare function prototypes
COLLATZ(DECL)

// Define function lookup table
int (*funs[])(int) = {
  COLLATZ(NAME)
};
const int funlen = sizeof(funs) / sizeof(*funs);

// Define the functions
COLLATZ(DEFN)

void print_version() {
  printf(MYNAME " %d.%d.%d", VER_MAJ, VER_MIN, VER_PATCH);
  if (*VER_REV)
    printf("+%s", VER_REV);
  printf("\n");
}

#define MAX_PROCS 1000
// Program entrypoint
int main (int c, char** v) {
  // Define and ingest parameters
  int ki = 1e1;
  int kj = 1e6;
  int t = 0;
  int n = 1+get_nprocs()/2;
  if (c > 1) {
    if (!strcmp(v[1], "-v") || !strcmp(v[1], "--version")) {
      print_version();
      return 0;
    } else if (!strcmp(v[1], "-h") || !strcmp(v[1], "--help")) {
      printf("collatz <CPUs> <outer index> <inner index> <target value>\n");
      printf("  CPUs -- number of CPUs to use (defaults to 1/2 + 1 of total)\n");
      printf("    0 -- use 1/2 + 1 CPUs\n");
      printf("   -1 -- use all cpus\n");
      printf("  outer/inner indices -- outer*inner = total loops\n");
      printf("  target -- value for collatz conjecture; otherwise uses every index from inner loop\n");
      printf("    Also supports the following special values (val; depth):\n");
      printf("      A -- (7; 16)\n");
      printf("      B -- (27; 111)\n");
      printf("      C -- (703; 170)\n");
      printf("      D -- (2463; 208)\n");
      printf("      E -- (6171; 261)\n");
      printf("      F -- (77031; 350) (from Wikipedia)\n");
      printf("      G -- (837799; 524) (from Wikipedia)\n");
      printf("      H -- (780657630; 1132) (from Wikipedia)\n");
      printf("    These values are from https://oeis.org/A006577/b006577.txt (table.txt)\n");
      return 0;
    }
    P(v[1], n);
    if (n < 0) n = get_nprocs();
    if (n > MAX_PROCS) n = MAX_PROCS;
  }
  if (c > 2)
    P(v[2], ki);
  if (c > 3)
    P(v[3], kj);
  if (c > 4) {
    switch(*v[4]) {
      case 'A': case 'a': t = 7;         break;
      case 'B': case 'b': t = 27;        break;
      case 'C': case 'c': t = 703;       break;
      case 'D': case 'd': t = 2463;      break;
      case 'E': case 'e': t = 6171;      break;
      case 'F': case 'f': t = 77031;     break;
      case 'G': case 'g': t = 837799;    break;
      case 'H': case 'h': t = 780657630; break;
      default:
        P(v[4], t);
    }
  }
  printf("%d, %d, %d, %d, ", n, ki, kj, t); fflush(stdout);

  // Setup
  static __thread unsigned long work_start, work_end;
  static __thread unsigned long last_counter = 0;
  unsigned long *start_tick = mmap(NULL, MAX_PROCS*sizeof(unsigned long), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  unsigned long *end_tick = mmap(NULL, MAX_PROCS*sizeof(unsigned long), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  pid_t pids[MAX_PROCS] = {0};
  pids[0] = getpid();
  counter = mmap(NULL, sizeof(unsigned long), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  *counter = 0;

  // Setup barrier for coordination
  pthread_barrierattr_t bat = {0};
  pthread_barrier_t *pb = mmap(NULL, sizeof(pthread_barrier_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  pthread_barrierattr_init(&bat);
  pthread_barrierattr_setpshared(&bat, 1);
  pthread_barrier_init(pb, &bat, n);


  // Execute
  int me = 0;
  for (int i=1; i<n && (pids[i] = fork()); i++) {me = i;}

  // Now that we're in a fork, set up my local statsd socket
  int fd_statsd = -1;
  char *path_statsd = NULL;
  if ((path_statsd = getenv("DD_DOGSTATSD_SOCKET")))
    fd_statsd = statsd_open(path_statsd, strlen(path_statsd));

  // OK, so we want to wait until everyone has started, but if we have more
  // work than we have cores, we might realistically start after other workers
  // have started.  So need to double-tap the barrier.
  pthread_barrier_wait(pb);
  start_tick[me] = __rdtsc();
  pthread_barrier_wait(pb);
  for (int j=0; j<ki; j++) {

    work_start = __rdtsc();
    for (int i=0; i<kj; i++) {
      int arg = t ? t : i;
      funs[arg%funlen](arg);
    }

    // Print to statsd, if configured
    if (-1 != fd_statsd) {
      work_end = __rdtsc();
      static char key_ticks[] = "app.collatz.ticks";
      static char key_stacks[] = "app.collatz.stacks";
      static char key_funs[] = "app.collatz.functions";
      statsd_send(fd_statsd, key_ticks, &(long){work_end - work_start}, STAT_GAUGE);
      statsd_send(fd_statsd, key_stacks, &kj, STAT_GAUGE);
      statsd_send(fd_statsd, key_funs, &(long){*my_counter-last_counter}, STAT_GAUGE); // technically can overflow, but whatever
      last_counter = *my_counter;
    }
  }

  // Wait for everyone to be done
  __sync_add_and_fetch(counter,*my_counter);
  pthread_barrier_wait(pb);
  end_tick[me] = __rdtsc();
  pthread_barrier_wait(pb);
  if (getpid() != pids[0]) return 0;
  unsigned long long ticks = 0;
  for (int i=0; i<n; i++)
    ticks += end_tick[i] - start_tick[i];

  // Print results
  if (getpid() == pids[0]) {
    printf("%ld, %llu, %f\n", *counter, ticks, ((double)ticks)/((double)*counter));
  }
  return 0;
}
