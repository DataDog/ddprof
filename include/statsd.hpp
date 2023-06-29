// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h>
#include <string_view>

#include "ddres_def.hpp"

typedef enum STAT_TYPES {
  STAT_MS_LONG,
  STAT_MS_FLOAT,
  STAT_COUNT,
  STAT_GAUGE,
} STAT_TYPES;

/// Connect to a statsd server, returning a ddres and populating the passed
/// pointer on success
DDRes statsd_connect(std::string_view statsd_socket, int *);

/// Send the stats in a statsd format, returns a ddres
DDRes statsd_send(int, const char *, void *, int);

/// Close the socket, returns a ddres with matching status
DDRes statsd_close(int);

/* Private */
DDRes statsd_listen(std::string_view path, int *fd);
