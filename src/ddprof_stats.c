#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <ddprof_stats.h>

// Expand the statsd paths
#define X_PATH(a, b, c) "datadog.profiling.native." b,
static const char *stats_paths[] = {STATS_TABLE(X_PATH)};
#undef X_PATH

// Expand the types
#define X_TYPES(a, b, c) c,
static const unsigned int stats_types[] = {STATS_TABLE(X_TYPES)};
#undef X_TYPES

// File descriptor for statsd
static int fd_statsd = -1;

// Region (to be mmap'd here) for backend store
long *ddprof_stats = NULL;

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

  ddprof_stats = mmap(NULL, sizeof(long) * STATS_LEN, PROT_READ | PROT_WRITE,
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

DDRes ddprof_stats_add(unsigned int stat, long in, long *out) {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");
  if (stat >= STATS_LEN)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Invalid stat");

  long retval = __sync_add_and_fetch(&ddprof_stats[stat], in);

  if (out)
    *out = retval;
  return ddres_init();
}

DDRes ddprof_stats_set(unsigned int stat, long n) {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");
  if (stat >= STATS_LEN)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Invalid stat");
  ddprof_stats[stat] = n;
  return ddres_init();
}

DDRes ddprof_stats_clear(unsigned int stat) {
  return ddprof_stats_set(stat, 0);
}

DDRes ddprof_stats_clear_all() {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");

  // Note:  we leave the DDRes returns here uncollected, since the loop bounds
  //        are strongly within the ddprof_stats bounds and we've already
  //        verified the presence of the backend store.  These are the only two
  //        non-success criteria for the individual clear operations.
  for (int i = 0; i < STATS_LEN; i++)
    ddprof_stats_clear(i);

  return ddres_init();
}

DDRes ddprof_stats_get(unsigned int stat, long *out) {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");
  if (stat >= STATS_LEN)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Invalid stat");

  if (out)
    *out = ddprof_stats[stat];
  return ddres_init();
}

DDRes ddprof_stats_send(void) {
  for (unsigned int i = 0; i < STATS_LEN; i++) {
    DDRES_CHECK_FWD(statsd_send(fd_statsd, stats_paths[i], &ddprof_stats[i],
                                stats_types[i]));
  }
  return ddres_init();
}

void ddprof_stats_print() {
  for (unsigned int i = 0; i < STATS_LEN; i++)
    LG_NTC("[STATS] %s: %ld", stats_paths[i], ddprof_stats[i]);
}
