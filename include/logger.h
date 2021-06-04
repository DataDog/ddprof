#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

typedef enum LOG_OPTS {
  LOG_DISABLE = 0,
  LOG_SYSLOG = 1,
  LOG_STDOUT = 2,
  LOG_STDERR = 3,
  LOG_FILE = 4,
} LOG_OPTS;

typedef enum LOG_LVL {
  LL_EMERGENCY = 0,
  LL_ALERT = 1,
  LL_CRITICAL = 2,
  LL_ERROR = 3,
  LL_WARNING = 4,
  LL_NOTICE = 5,
  LL_INFORMATIONAL = 6,
  LL_DEBUG = 7,
  LL_LENGTH
} LOG_LVL;

typedef enum LOG_FACILITY {
  LF_KERNEL = 0,
  LF_USER = 1,
  LF_MAIL = 2,
  LF_SYSTEM = 3,
  LF_SECURITY = 4,
  LF_SYSLOGD = 5,
  LF_LINE = 6,
  LF_NETNEWS = 7,
  LF_UUCP = 8,
  LF_CLOCK = 9,
  LF_SEC2 = 10,
  LF_FTP = 11,
  LF_NTP = 12,
  LF_AUDIT = 13,
  LF_ALERT = 14,
  LF_CLOCK2 = 15,
  LF_LOCAL0 = 16,
  LF_LOCAL1 = 17,
  LF_LOCAL2 = 18,
  LF_LOCAL3 = 19,
  LF_LOCAL4 = 20,
  LF_LOCAL5 = 21,
  LF_LOCAL6 = 22,
  LF_LOCAL7 = 23,
} LOGF_FACILITY;

bool LOG_syslog_open();
void LOG_close();
bool LOG_open(int, char *);
__attribute__((format(printf, 4, 5))) void LOG_lfprintf(int, int, char *,
                                                        const char *, ...);
bool LOG_setname(char *);
void LOG_setlevel(int);
int LOG_getlevel();
void LOG_setfacility(int);

#endif
