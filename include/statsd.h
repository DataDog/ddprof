#ifndef _H_ipc
#define _H_ipc

#include <stdbool.h>
#include <stddef.h>

typedef enum STAT_TYPES {
  STAT_MS_LONG,
  STAT_MS_FLOAT,
  STAT_COUNT,
} STAT_TYPES;

int statsd_open(char *, size_t);
bool statsd_send(int, char *, size_t, void *, int);
bool statsd_close(int);

#endif
