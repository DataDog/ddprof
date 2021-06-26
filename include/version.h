#ifndef _H_VERSION
#define _H_VERSION

#ifndef MYNAME
#  define MYNAME "ddprof"
#endif
#define VER_MAJ 0
#define VER_MIN 4
#define VER_PATCH 7
#ifndef VER_REV
#  define VER_REV "custom"
#endif

const char *str_version();

void print_version();

#endif
