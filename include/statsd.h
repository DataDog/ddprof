#ifndef _H_statsd
#define _H_statsd

#include <stdbool.h>
#include <stddef.h>

typedef enum STAT_TYPES {
  STAT_MS_LONG,
  STAT_MS_FLOAT,
  STAT_COUNT,
  STAT_GAUGE,
} STAT_TYPES;

int statsd_open(char *, size_t);
bool statsd_send(int, char *, void *, int);
bool statsd_close(int);

#endif
