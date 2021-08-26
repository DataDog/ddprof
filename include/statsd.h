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

/// Connect to a statsd server, returning a ddres and populating the passed
/// pointer on success
DDRes statsd_connect(const char *, size_t, int *);

/// Send the stats in a statsd format, returns a ddres
DDRes statsd_send(int, const char *, void *, int);

/// Close the socket, returns a ddres with matching status
DDRes statsd_close(int);

/* Private */
DDRes statsd_listen(const char *path, size_t sz_path, int *fd);
