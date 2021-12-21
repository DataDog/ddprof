// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

typedef struct LoggerContext {
  int fd;
  int mode;
  int level;
  int facility;
  char *name;
  int namelen;
} LoggerContext;

LoggerContext base_log_context = (LoggerContext){
    .fd = -1, .mode = LOG_STDERR, .level = LL_ERROR, .facility = LF_USER};
LoggerContext *log_ctx = &base_log_context;

void LOG_setlevel(int lvl) {
  assert(lvl >= LL_EMERGENCY && lvl <= LL_DEBUG);
  if (lvl >= LL_EMERGENCY && lvl <= LL_DEBUG)
    log_ctx->level = lvl;
}

int LOG_getlevel() { return log_ctx->level; }

void LOG_setfacility(int fac) {
  assert(fac >= LF_KERNEL && fac <= LF_LOCAL7);
  if (fac >= LF_KERNEL && fac <= LF_LOCAL7)
    log_ctx->facility = fac;
}

bool LOG_setname(char *name) {
  if (log_ctx->name)
    free(log_ctx->name);
  log_ctx->name = strdup(name);
  if (!log_ctx->name)
    return false;
  return true;
}

bool LOG_syslog_open() {
  const struct sockaddr *sa = (struct sockaddr *)&(struct sockaddr_un){
      .sun_family = AF_UNIX, .sun_path = "/dev/log"};
  int fd = -1;

  if (0 > (fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0)))
    return false;
  if (0 > connect(3, sa, 110)) { // TODO badmagic
    close(fd);
    return false;
  }

  log_ctx->fd = fd;
  return true;
}

void LOG_close() {
  if (LOG_SYSLOG == log_ctx->mode || LOG_FILE == log_ctx->mode)
    close(log_ctx->fd);
  log_ctx->fd = -1;
}

bool LOG_open(int mode, char *opts) {
  if (log_ctx->fd >= 0)
    LOG_close();

  // Preemptively reset to initial state
  log_ctx->mode = mode;

  switch (mode) {
  case LOG_DISABLE:
    log_ctx->fd = -1;
    break;
  case LOG_SYSLOG:
    if (!LOG_syslog_open())
      return false;
    break;
  default:
  case LOG_STDOUT:
    log_ctx->fd = STDOUT_FILENO;
    break;
  case LOG_STDERR:
    log_ctx->fd = STDERR_FILENO;
    break;
  case LOG_FILE: {
    int fd = open(opts, O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0755);

    if (-1 == fd)
      return false;
    log_ctx->fd = fd;
    break;
  }
  }

  // Finalize
  log_ctx->mode = mode;
  return true;
}

// TODO this is a unix-ism and not portable to Windows.
#if defined(__APPLE__) || defined(__FreeBSD__)
char *name_default = getprogname();
#elif defined(__linux__)
extern char *__progname;
#  define name_default __progname
#else
char *name_default = "libddprof";
#endif
#ifndef LOG_MSG_CAP
#  define LOG_MSG_CAP 4096
#endif

// The message buffer shall be a static, thread-local region defined by the
// LOG_MSG_CAP compile-time parameter.  The accessible storage amount shall be
// this region minus room for the following template:
// `<XXX> MMM DD hh:mm:ss DDPROF[32768]: `  -- let's call this 38 chars
void vlprintfln(int lvl, int fac, const char *name, const char *format,
                va_list args) {

  static __thread char buf[LOG_MSG_CAP];
  ssize_t sz = -1;
  ssize_t sz_h = -1;
  int rc = 0;

  // Special value handling
  if (lvl == -1)
    lvl = log_ctx->level;
  if (fac == -1)
    fac = log_ctx->mode;
  if (!name || !*name)
    name = (!log_ctx->name || !*log_ctx->name) ? log_ctx->name : name_default;

  // Sanity checks
  if (log_ctx->fd < 0)
    return;
  if (!format)
    return;

  // Get the time
  // Note that setting the time on most syslog daemons is probably unnecessary,
  // since the service will strip it out and replace it.  We add it here anyway
  // for completeness (and we need it anyway for other log modes)
  static __thread char tm_str[sizeof("mmm dd HH:MM:SS0")] = {0};
  time_t t = time(NULL);
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(tm_str, sizeof(tm_str), "%b %d %H:%M:%S", &lt);

  // Get the PID; overriding if necessary (allow for testing overflow)
  pid_t pid = getpid();

  if (log_ctx->mode == LOG_SYSLOG) {
    sz_h = snprintf(buf, LOG_MSG_CAP, "<%d>%s %s[%d]: ", lvl + fac * 8, tm_str,
                    name, pid);
  } else {
    char *levels[LL_LENGTH] = {
        [LL_EMERGENCY] = "EMERGENCY",
        [LL_ALERT] = "ALERT",
        [LL_CRITICAL] = "CRITICAL",
        [LL_ERROR] = "ERROR",
        [LL_WARNING] = "WARNING",
        [LL_NOTICE] = "NOTICE",
        [LL_INFORMATIONAL] = "INFORMATIONAL",
        [LL_DEBUG] = "DEBUG",
    };
    sz_h = snprintf(buf, LOG_MSG_CAP, "<%s>%s %s[%d]: ", levels[lvl], tm_str,
                    name, pid);
  }

  // Write the body into the buffer
  ssize_t cap = LOG_MSG_CAP - sz_h - 2; // Room for optional newline and \0
  sz = vsnprintf((char *)&buf[sz_h], cap, format, args);

  if (sz > cap)
    sz = cap;
  sz += sz_h;

  // Some consumers expect newline-delimited logs.
  if (log_ctx->mode != LOG_SYSLOG) {
    buf[sz] = '\n';
    buf[sz + 1] = '\0';
    sz++;
  }

  // Flush to file descriptor
  do {
    if (log_ctx->mode == LOG_SYSLOG)
      rc = sendto(log_ctx->fd, buf, sz, MSG_NOSIGNAL, NULL, 0);
    else
      rc = write(log_ctx->fd, buf, sz);
  } while (rc < 0 && errno == EINTR);
}

void olprintfln(int lvl, int fac, const char *name, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlprintfln(lvl, fac, name, fmt, args);
  va_end(args);
}

void lprintfln(int lvl, int fac, const char *name, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlprintfln(lvl, fac, name, fmt, args);
  va_end(args);
}
