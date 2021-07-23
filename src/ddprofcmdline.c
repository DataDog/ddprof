#include "ddprofcmdline.h"

#include <assert.h>
#include <strings.h>

int arg_which(const char *str, char const *const *set, int sz_set) {
  if (!str || !set)
    return -1;
  for (int i = 0; i < sz_set; i++) {
    if (set[i] && !strcasecmp(str, set[i]))
      return i;
  }
  return -1;
}

bool arg_inset(const char *str, char const *const *set, int sz_set) {
  int ret = arg_which(str, set, sz_set);
  return !(-1 == ret);
}

bool arg_yesno(const char *str, int mode) {
  const int sizeOfPaterns = 3;
  static const char *yes_set[] = {"yes", "true", "on"}; // mode = 1
  static const char *no_set[] = {"no", "false", "off"}; // mode = 0
  assert(0 == mode || 1 == mode);
  char const *const *set = (!mode) ? no_set : yes_set;
  if (arg_which(str, set, sizeOfPaterns) != -1) {
    return true;
  }
  return false;
}

bool process_event(const char *str, char **lookup, size_t sz_lookup,
                   size_t *idx, int *value) {
  size_t sz_str = strlen(str);

  for (int i = 0; i < sz_lookup; ++i) {
    size_t sz_key = strlen(perfoptions[i].key);
    if (!strncmp(perfoptions[i].key, str, sz_key)) {
      ctx->watchers[ctx->num_watchers] = perfoptions[i];

      double sample_period = 0.0;
      if (sz_str > sz_key && str[sz_str] == ',')
        sample_period = strtod(&str[sz_key + 1], NULL);
      if (sample_period > 0)
        ctx->watchers[ctx->num_watchers].sample_period = sample_period;

      ++ctx->num_watchers;
      return true;
    }
  }

  return false;
}
