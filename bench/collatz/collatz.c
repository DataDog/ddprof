#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <x86intrin.h>

unsigned long* counter = NULL;
unsigned long* my_counter = &(unsigned long){0};

#define P(s, d) ({                \
  long _t = strtoll(s, NULL, 10); \
  if (_t) d = _t;                 \
})
#define D(f) int f(int x)
#define F(x) {                            \
  int n = x&1 ? x*3+1 : x/2;              \
  __sync_add_and_fetch(my_counter, 1);    \
  return 1 >= n ? 1 : funs[n%funlen](n);  \
}

D(fun00); D(fun01); D(fun02); D(fun03); D(fun04); D(fun05); D(fun06); D(fun07); D(fun08); D(fun09);
D(fun10); D(fun11); D(fun12); D(fun13); D(fun14); D(fun15); D(fun16); D(fun17); D(fun18); D(fun19);
D(fun20); D(fun21); D(fun22); D(fun23); D(fun24); D(fun25); D(fun26); D(fun27); D(fun28); D(fun29);
D(fun30); D(fun31); D(fun32); D(fun33); D(fun34); D(fun35); D(fun36); D(fun37); D(fun38); D(fun39);
D(fun40); D(fun41); D(fun42); D(fun43); D(fun44); D(fun45); D(fun46); D(fun47); D(fun48); D(fun49);
D(fun50); D(fun51); D(fun52); D(fun53); D(fun54); D(fun55); D(fun56); D(fun57); D(fun58); D(fun59);
D(fun60); D(fun61); D(fun62); D(fun63); D(fun64); D(fun65); D(fun66); D(fun67); D(fun68); D(fun69);
D(fun70); D(fun71); D(fun72); D(fun73); D(fun74); D(fun75); D(fun76); D(fun77); D(fun78); D(fun79);
D(fun80); D(fun81); D(fun82); D(fun83); D(fun84); D(fun85); D(fun86); D(fun87); D(fun88); D(fun89);
D(fun90); D(fun91); D(fun92); D(fun93); D(fun94); D(fun95); D(fun96); D(fun97); D(fun98); D(fun99);

int (*funs[])(int) = {
  fun00, fun01, fun02, fun03, fun04, fun05, fun06, fun07, fun08, fun09,
  fun10, fun11, fun12, fun13, fun14, fun15, fun16, fun17, fun18, fun19,
  fun20, fun21, fun22, fun23, fun24, fun25, fun26, fun27, fun28, fun29,
  fun30, fun31, fun32, fun33, fun34, fun35, fun36, fun37, fun38, fun39,
  fun40, fun41, fun42, fun43, fun44, fun45, fun46, fun47, fun48, fun49,
  fun50, fun51, fun52, fun53, fun54, fun55, fun56, fun57, fun58, fun59,
  fun60, fun61, fun62, fun63, fun64, fun65, fun66, fun67, fun68, fun69,
  fun70, fun71, fun72, fun73, fun74, fun75, fun76, fun77, fun78, fun79,
  fun80, fun81, fun82, fun83, fun84, fun85, fun86, fun87, fun88, fun89,
  fun90, fun91, fun92, fun93, fun94, fun95, fun96, fun97, fun98, fun99,
};

#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

const int funlen = sizeof(funs) / sizeof(*funs);

D(fun00) F(x); D(fun01) F(x); D(fun02) F(x); D(fun03) F(x); D(fun04) F(x); D(fun05) F(x); D(fun06) F(x); D(fun07) F(x); D(fun08) F(x); D(fun09) F(x);
D(fun10) F(x); D(fun11) F(x); D(fun12) F(x); D(fun13) F(x); D(fun14) F(x); D(fun15) F(x); D(fun16) F(x); D(fun17) F(x); D(fun18) F(x); D(fun19) F(x);
D(fun20) F(x); D(fun21) F(x); D(fun22) F(x); D(fun23) F(x); D(fun24) F(x); D(fun25) F(x); D(fun26) F(x); D(fun27) F(x); D(fun28) F(x); D(fun29) F(x);
D(fun30) F(x); D(fun31) F(x); D(fun32) F(x); D(fun33) F(x); D(fun34) F(x); D(fun35) F(x); D(fun36) F(x); D(fun37) F(x); D(fun38) F(x); D(fun39) F(x);
D(fun40) F(x); D(fun41) F(x); D(fun42) F(x); D(fun43) F(x); D(fun44) F(x); D(fun45) F(x); D(fun46) F(x); D(fun47) F(x); D(fun48) F(x); D(fun49) F(x);
D(fun50) F(x); D(fun51) F(x); D(fun52) F(x); D(fun53) F(x); D(fun54) F(x); D(fun55) F(x); D(fun56) F(x); D(fun57) F(x); D(fun58) F(x); D(fun59) F(x);
D(fun60) F(x); D(fun61) F(x); D(fun62) F(x); D(fun63) F(x); D(fun64) F(x); D(fun65) F(x); D(fun66) F(x); D(fun67) F(x); D(fun68) F(x); D(fun69) F(x);
D(fun70) F(x); D(fun71) F(x); D(fun72) F(x); D(fun73) F(x); D(fun74) F(x); D(fun75) F(x); D(fun76) F(x); D(fun77) F(x); D(fun78) F(x); D(fun79) F(x);
D(fun80) F(x); D(fun81) F(x); D(fun82) F(x); D(fun83) F(x); D(fun84) F(x); D(fun85) F(x); D(fun86) F(x); D(fun87) F(x); D(fun88) F(x); D(fun89) F(x);
D(fun90) F(x); D(fun91) F(x); D(fun92) F(x); D(fun93) F(x); D(fun94) F(x); D(fun95) F(x); D(fun96) F(x); D(fun97) F(x); D(fun98) F(x); D(fun99) F(x);

#define MAX_PROCS 1000
int main (int c, char** v) {
  // Define and ingest parameters
  int ki = 1e1;
  int kj = 1e6;
  int t = 0;
  int n = 1+get_nprocs()/2;
  if (c > 1) {
    P(v[1], n);
    if (n > MAX_PROCS) n = MAX_PROCS;
  }
  if (c > 2)
    P(v[2], ki);
  if (c > 3)
    P(v[3], kj);
  if (c > 4)
    P(v[4], t);
  printf("%d, %d, %d, %d, ", n, ki, kj, t); fflush(stdout);

  // Setup
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

  // OK, so we want to wait until everyone has started, but if we have more
  // work than we have cores, we might realistically start after other workers
  // have started.  So need to double-tap the barrier.
  pthread_barrier_wait(pb);
  start_tick[me] = __rdtsc();
  pthread_barrier_wait(pb);
  for (int j=0; j<ki; j++)
    for (int i=0; i<kj; i++)
      fun00(t ? t : i);

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
