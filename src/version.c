#include "version.h"

#include <stdio.h>

const char *str_version() {
  static char version[1024] = {0};
  if (*VER_REV)
    snprintf(version, 1024, "%d.%d.%d+%s", VER_MAJ, VER_MIN, VER_PATCH,
             VER_REV);
  else
    snprintf(version, 1024, "%d.%d.%d", VER_MAJ, VER_MIN, VER_PATCH);
  return version;
}

void print_version() { printf(MYNAME " %s\n", str_version()); }
