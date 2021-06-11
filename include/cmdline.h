#pragma once

// c99
#include <stdbool.h>

int arg_which(char *str, char **set, int sz_set);

#define arg_whichmember(str, set)                                              \
  arg_which(str, set, sizeof(set) / sizeof(*set))

bool arg_inset(char *str, char **set, int sz_set);

bool arg_yesno(char *str, int mode);
