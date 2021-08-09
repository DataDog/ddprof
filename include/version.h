#pragma once

// Name and versions are defined in build system
#ifndef MYNAME
#  define MYNAME "ddprof"
#endif

#ifndef VER_MAJ
#  define VER_MAJ 0
#endif
#ifndef VER_MIN
#  define VER_MIN 0
#endif
#ifndef VER_PATCH
#  define VER_PATCH 0
#endif
#ifndef VER_REV
#  define VER_REV "custom"
#endif

/// Versions are updated in cmake files
const char *str_version();

void print_version();
