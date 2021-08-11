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

int statsd_listen(const char *, size_t);
int statsd_connect(const char *, size_t);
bool statsd_send(int, const char *, void *, int);
bool statsd_close(int);

#endif
