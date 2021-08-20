#pragma once

#include <stdbool.h>

#include "logger.h"
#include "statsd.h"

typedef union StatsValue {
  long l;
  long Long;
  double d;
  double Double;
} StatsValue;

#define X_ENUM(a, b, c) STATS_##a,
#define STATS_TABLE(X)                                                         \
  X(EVENT_COUNT, "event.count", STAT_GAUGE)                                    \
  X(EVENT_LOST, "event.lost", STAT_GAUGE)                                      \
  X(SAMPLE_COUNT, "sample.count", STAT_GAUGE)                                  \
  X(UNWIND_TICKS, "unwind.ticks", STAT_GAUGE)                                  \
  X(PROCFS_RSS, "procfs.rss", STAT_GAUGE)                                      \
  X(PROCFS_UTIME, "procfs.utime", STAT_GAUGE)                                  \
  X(PPROF_ST_ELEMS, "pprof.st_elements", STAT_GAUGE)

// Expand the enum/index for the individual stats
typedef enum DDPROF_STATS { STATS_TABLE(X_ENUM) STATS_LEN } DDPROF_STATS;
#undef X_ENUM

// Expand the types for each stat
#define X_TYPE(a, b, c) c,
unsigned int stats_types[] = {STATS_TABLE(X_TYPE)};
#undef X_TYPE

// Necessary for initializing the backend store for stats.  It's necessary that
// this is called prior to any fork() calls where the children might want to use
// stats, but it's fine to call this after forks have spawned.
bool ddprof_stats_init();

// Whereas the regions are inherited by forks, freeing the backend store is not.
// That means that free() must be called from every process wishing to clean
// its own store (although this is a small array of longs, so probably not too
// important)
void ddprof_stats_free();

// The add operator is multithread- and multiprocess-safe, even though the
// library doesn't really use either as a form of concurrency.
// Negative values are allowable.
long ddprof_stats_addl(unsigned int stat, long n);
double ddprof_stats_addf(unsigned int stat, double x);

// This is a gcc statement expression in order to perform the correct ADD
// operation for the specified type, returning the correct type.  Compatible
// with clang
#define ddprof_stats_add(stat, v)                                              \
  __extension__({                                                              \
    __typeof__(v) ret;                                                         \
    if (stat > STATS_LEN)                                                      \
      ret = 0;                                                                 \
    else                                                                       \
      ret = STAT_MS_FLOAT == stats_types[stat] ? ddprof_stats_addf(stat, v)    \
                                               : ddprof_stats_addl(stat, v);   \
    ret;                                                                       \
  })

// Setting and clearing are last-through-the-gate operations with ties broken
// by whatever the CPU is executing at that time.  This is pretty standard, but
// it's worth noting that these operations propagate to all consumers.
long ddprof_stats_set(unsigned int stat, long n);
void ddprof_stats_clear(unsigned int stat);

// Merely gets the value of the statistic.
long ddprof_stats_get(unsigned int stat);
bool ddprof_stats_send();
