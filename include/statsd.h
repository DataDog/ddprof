#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ddres_def.h"

typedef enum STAT_TYPES {
  STAT_MS_LONG,
  STAT_MS_FLOAT,
  STAT_COUNT,
  STAT_GAUGE,
} STAT_TYPES;

/// connect to a socket, returns -1 on failure, file descriptor on success
int statsd_connect(const char *, size_t);

/// Send the stats in a statsd format, returns a ddres
DDRes statsd_send(int, const char *, void *, int);

/// Close the socket, returns a ddres with matching status
DDRes statsd_close(int);

/* Private */
int statsd_listen(const char *path, size_t sz_path);
