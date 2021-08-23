#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <ddprof_stats.h>

// Expand the statsd paths
#define X_PATH(a, b, c) "datadog.profiling.native." b,
static const char *stats_paths[] = {STATS_TABLE(X_PATH)};

// File descriptor for statsd
static int fd_statsd = -1;

StatsValue *ddprof_stats = NULL;

// Helper function for getting statsd connection
int statsd_init() {
  char *path_statsd = NULL;
  if ((path_statsd = getenv("DD_DOGSTATSD_SOCKET"))) {
    fd_statsd = statsd_connect(path_statsd, strlen(path_statsd));
    if (-1 != fd_statsd) {
      return fd_statsd;
    }
  }
  return -1;
}

bool ddprof_stats_init() {
  // This interface cannot be used to reset the existing mapping; to do so free
  // and then re-initialize.
  if (ddprof_stats)
    return true;

  ddprof_stats =
      mmap(NULL, sizeof(StatsValue) * STATS_LEN, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == ddprof_stats) {
    // error handling here
  }

  fd_statsd = statsd_init();
  if (-1 == fd_statsd) {
    // error handling here
  }
}

void ddprof_stats_free() {
  if (ddprof_stats)
    munmap(ddprof_stats, sizeof(long) * STATS_LEN);
  ddprof_stats = NULL;
}

long ddprof_stats_addl(unsigned int stat, long n) {
  if (!ddprof_stats)
    return 0;
  if (stat >= STATS_LEN)
    return 0;
  return __sync_add_and_fetch(&ddprof_stats[stat].l, n);
}

double ddprof_stats_addf(unsigned int stat, double x) {
  if (!ddprof_stats)
    return 0;
  if (stat >= STATS_LEN)
    return 0.0;

  double *oldval;
  double *newval;
  *oldval = ddprof_stats[stat].d;
  *newval = *oldval + x;

  int spinlock_count = 3 + 1;
  while (!__sync_bool_compare_and_swap(&ddprof_stats[stat].l, *(long *)oldval,
                                       *(long *)newval) &&
         --spinlock_count) {
    *oldval = ddprof_stats[stat].d;
    *newval = *oldval + x;
  }

  if (!spinlock_count)
    return 0.0;

  return *newval;
}

long ddprof_stats_setl(unsigned int stat, long n) {
  if (!ddprof_stats)
    return 0;
  if (stat >= STATS_LEN)
    return 0;
  ddprof_stats[stat].l = n;
  return n;
}

double ddprof_stats_setf(unsigned int stat, double x) {
  if (!ddprof_stats)
    return 0;
  if (stat >= STATS_LEN)
    return 0.0;
  ddprof_stats[stat].d = x;
  return x;
}

// IEEE-754 0 is the same as integral 0
void ddprof_stats_clear(unsigned int stat) { ddprof_stats_setl(stat, 0); }

void ddprof_stats_clear_all() {
  if (!ddprof_stats)
    return;
  for (int i = 0; i < STATS_LEN; i++)
    ddprof_stats_clear(i);
}

long ddprof_stats_getl(unsigned int stat) {
  if (!ddprof_stats)
    return 0;
  if (stat >= STATS_LEN)
    return 0;
  return ddprof_stats[stat].l;
}

long ddprof_stats_getf(unsigned int stat) {
  if (!ddprof_stats)
    return 0;
  if (stat >= STATS_LEN)
    return 0.0;
  return ddprof_stats[stat].d;
}

bool ddprof_stats_send() {
  for (unsigned int i = 0; i < STATS_LEN; i++) {
    if (statsd_send(fd_statsd, stats_paths[i], &ddprof_stats[i].l,
                    stats_types[i])) {
      // error handling here
    }
  }
}

void ddprof_stats_print() {
  for (unsigned int i = 0; i < STATS_LEN; i++) {
    if (stats_types[i] == STAT_MS_FLOAT)
      LG_NTC("[STATS] %s: %f", stats_paths[i], ddprof_stats[i].d);
    else
      LG_NTC("[STATS] %s: %ld", stats_paths[i], ddprof_stats[i].l);
  }
}
