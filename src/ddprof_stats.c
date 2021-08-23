#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <ddprof_stats.h>

// Expand the statsd paths
#define X_PATH(a, b, c) "datadog.profiling.native." b,
static const char *stats_paths[] = {STATS_TABLE(X_PATH)};

// Expand the types for each stat
#define X_TYPE(a, b, c) c,
static const STAT_TYPES stats_types[] = {STATS_TABLE(X_TYPE)};
#undef X_TYPE

// File descriptor for statsd
static int fd_statsd = -1;

StatsValue *ddprof_stats = NULL;

// Helper function for getting statsd connection
DDRes statsd_init() {
  char *path_statsd = NULL;
  if ((path_statsd = getenv("DD_DOGSTATSD_SOCKET"))) {
    fd_statsd = statsd_connect(path_statsd, strlen(path_statsd));
    if (-1 != fd_statsd) {
      DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS,
                            "Unable to establish statsd connection");
    }
  }
  return ddres_init();
}

DDRes ddprof_stats_init() {
  // This interface cannot be used to reset the existing mapping; to do so free
  // and then re-initialize.
  if (ddprof_stats)
    return ddres_init();

  ddprof_stats =
      mmap(NULL, sizeof(StatsValue) * STATS_LEN, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == ddprof_stats) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_DDPROF_STATS, "Unable to mmap for stats");
  }
  DDRES_CHECK_FWD(statsd_init());
  return ddres_init();
}

DDRes ddprof_stats_free() {
  if (ddprof_stats)
    DDRES_CHECK_INT(munmap(ddprof_stats, sizeof(long) * STATS_LEN),
                    DD_WHAT_DDPROF_STATS, "Error from munmap");
  ddprof_stats = NULL;

  if (fd_statsd != -1) {
    DDRES_CHECK_FWD(statsd_close(fd_statsd));
    fd_statsd = -1;
  }
  return ddres_init();
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

DDRes ddprof_stats_send(void) {
  for (unsigned int i = 0; i < STATS_LEN; i++) {
    DDRES_CHECK_FWD(statsd_send(fd_statsd, stats_paths[i], &ddprof_stats[i].l,
                                stats_types[i]));
  }
  return ddres_init();
}

STAT_TYPES ddprof_stats_gettype(DDPROF_STATS stats) {
  return stats_types[stats];
}

void ddprof_stats_print() {
  for (unsigned int i = 0; i < STATS_LEN; i++) {
    if (stats_types[i] == STAT_MS_FLOAT)
      LG_NTC("[STATS] %s: %f", stats_paths[i], ddprof_stats[i].d);
    else
      LG_NTC("[STATS] %s: %ld", stats_paths[i], ddprof_stats[i].l);
  }
}
