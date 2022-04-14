// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <stdlib.h>
#include <unistd.h>

#include "dd_profiling.h"

int main(int, char *[]) {
  ddprof_start_profiling();
  while (true) {
    void *p = malloc(1000);
    void *p2 = realloc(p, 2000);
    free(p2);
    void *p3 = calloc(1, 512);
    (void)p3;
    free(p3);
    usleep(1000);
  }
}
