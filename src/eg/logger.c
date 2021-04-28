#include "logger.h"
#define LOG_MSG_CAP 4096

#include <string.h>

int main() {
  LOG_open(LOG_SYSLOG, "");
  LOG_setlevel(LL_DEBUG);
  char buf[2 * LOG_MSG_CAP];
  int left = LOG_MSG_CAP - 2;
  int right = left + 4;

  for (int i = left; i < right; i++) {
    memset(buf, 0, sizeof(buf));
    memset(buf, 'A' + (i % 25), i);
    buf[i] = '|';
    LOG_lfprintf(-1, -1, "", buf);
  }

  LOG_open(LOG_STDOUT, "");
  for (int i = left; i < right; i++) {
    memset(buf, 0, sizeof(buf));
    memset(buf, 'A' + (i % 25), i);
    buf[i] = '|';
    LOG_lfprintf(-1, -1, "", buf);
  }

  LOG_open(LOG_DISABLE, "");
  LOG_lfprintf(-1, -1, "", "HEELLOOOO");

  return 0;
}
