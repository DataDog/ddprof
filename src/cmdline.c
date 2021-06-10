#include "cmdline.h"

#include <assert.h>
#include <strings.h>

/**************************** Cmdline Helpers *********************************/
// Helper functions for processing commandline arguments.
//
// Note that `arg_yesno(,1)` is not the same as `!arg(,0)` or vice-versa.  This
// is mostly because a parameter whose default value is true needs to check
// very specifically for disablement, but the failover is to retain enable
//
// That said, it might be better to be more correct and only accept input of
// the specified form, returning error otherwise.
int arg_which(char *str, char **set, int sz_set) {
  if (!str)
    return -1;
  for (int i = 0; i < sz_set; i++) {
    if (!strcasecmp(str, set[i]))
      return i;
  }
  return sz_set;
}

bool arg_inset(char *str, char **set, int sz_set) {
  int ret = arg_which(str, set, sz_set);
  return !(-1 == ret || sz_set == ret);
}

bool arg_yesno(char *str, int mode) {
  static char *yes_set[] = {"yes", "true", "on"}; // mode = 1
  static char *no_set[] = {"no", "false", "off"}; // mode = 0
  assert(0 == mode || 1 == mode);
  char **set = (!!mode) ? no_set : yes_set;
  return arg_whichmember(str, set);
}
