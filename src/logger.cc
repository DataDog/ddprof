// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "logger.hpp"

#include "ratelimiter.hpp"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// TODO this is a unix-ism and not portable to Windows.
#if defined(__APPLE__) || defined(__FreeBSD__)
char *name_default = getprogname();
#elif defined(__linux__)
extern char *__progname;
#  define name_default __progname
#else
char *name_default = "ddprof";
#endif
#ifndef LOG_MSG_CAP
#  define LOG_MSG_CAP 4096
#endif

namespace ddprof {

namespace {

struct LoggerContext {
  int fd{-1};
  int mode{LOG_STDERR};
  int level{LL_ERROR};
  int facility{LF_USER};
  std::string name;
  std::optional<IntervalRateLimiter> rate_limiter;
  LogsAllowedCallback logs_allowed_function;
};

LoggerContext log_ctx{.fd = -1, .mode = LOG_STDERR, .level = LL_ERROR};
} // namespace

void LOG_setlevel(int lvl) {
  assert(lvl >= LL_EMERGENCY && lvl <= LL_DEBUG);
  if (lvl >= LL_EMERGENCY && lvl <= LL_DEBUG) {
    log_ctx.level = lvl;
  }
}

int LOG_getlevel() { return log_ctx.level; }

void LOG_setfacility(int fac) {
  assert(fac >= LF_KERNEL && fac <= LF_LOCAL7);
  if (fac >= LF_KERNEL && fac <= LF_LOCAL7) {
    log_ctx.facility = fac;
  }
}

void LOG_setname(const char *name) { log_ctx.name = name; }

bool LOG_syslog_open() {
  const sockaddr_un sa = {AF_UNIX, "/dev/log"};
  int const fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

  if (0 > fd) {
    return false;
  }
  if (0 >
      connect(fd, reinterpret_cast<const struct sockaddr *>(&sa), sizeof(sa))) {
    close(fd);
    return false;
  }

  log_ctx.fd = fd;
  return true;
}

void LOG_close() {
  if (LOG_SYSLOG == log_ctx.mode || LOG_FILE == log_ctx.mode) {
    close(log_ctx.fd);
  }
  log_ctx.fd = -1;
}

bool LOG_open(int mode, const char *opts) {
  if (log_ctx.fd >= 0) {
    LOG_close();
  }

  // Preemptively reset to initial state
  log_ctx.mode = mode;

  switch (mode) {
  case LOG_DISABLE:
    log_ctx.fd = -1;
    break;
  case LOG_SYSLOG:
    if (!LOG_syslog_open()) {
      return false;
    }
    break;
  default:
  case LOG_STDOUT:
    log_ctx.fd = STDOUT_FILENO;
    break;
  case LOG_STDERR:
    log_ctx.fd = STDERR_FILENO;
    break;
  case LOG_FILE: {
    int const fd = open(opts, O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0755);

    if (-1 == fd) {
      return false;
    }
    log_ctx.fd = fd;
    break;
  }
  }

  // Finalize
  log_ctx.mode = mode;
  return true;
}

void LOG_setratelimit(uint64_t max_log_per_interval,
                      std::chrono::nanoseconds interval) {
  log_ctx.rate_limiter.emplace(max_log_per_interval, interval);
}

// The message buffer shall be a static, thread-local region defined by the
// LOG_MSG_CAP compile-time parameter.  The accessible storage amount shall be
// this region minus room for the following template:
// `<XXX> MMM DD hh:mm:ss DDPROF[32768]: `  -- let's call this 38 chars
void vlprintfln(int lvl, int fac, const char *format, va_list args) {

  char buf[LOG_MSG_CAP];
  ssize_t sz = -1;
  ssize_t sz_h = -1;
  int rc = 0;

  // Special value handling
  if (lvl == -1) {
    lvl = log_ctx.level;
  }
  if (fac == -1) {
    fac = log_ctx.facility;
  }
  const char *name =
      !log_ctx.name.empty() ? log_ctx.name.c_str() : name_default;

  // Sanity checks
  if (log_ctx.fd < 0) {
    return;
  }
  if (!format) {
    return;
  }

  // Get the time
  // Note that setting the time on most syslog daemons is probably unnecessary,
  // since the service will strip it out and replace it.  We add it here anyway
  // for completeness (and we need it anyway for other log modes)
  char tm_str[sizeof("mmm dd HH:MM:SS0")];
  auto d = std::chrono::system_clock::now().time_since_epoch();
  auto d_s = std::chrono::duration_cast<std::chrono::seconds>(d);
  auto d_us = std::chrono::duration_cast<std::chrono::microseconds>(d - d_s);

  time_t const t = d_s.count();
  struct tm lt;
  localtime_r(&t, &lt);
  (void)strftime(tm_str, sizeof(tm_str), "%b %d %H:%M:%S", &lt);

  // Get the PID; overriding if necessary (allow for testing overflow)
  pid_t const pid = getpid();

  if (log_ctx.mode == LOG_SYSLOG) {
    sz_h = snprintf(buf, LOG_MSG_CAP,
                    "<%d>%s.%06ld %s[%d]: ", lvl + fac * LL_LENGTH, tm_str,
                    d_us.count(), name, pid);
  } else {
    const char *levels[LL_LENGTH] = {
        [LL_EMERGENCY] = "EMERGENCY",
        [LL_ALERT] = "ALERT",
        [LL_CRITICAL] = "CRITICAL",
        [LL_ERROR] = "ERROR",
        [LL_WARNING] = "WARNING",
        [LL_NOTICE] = "NOTICE",
        [LL_INFORMATIONAL] = "INFORMATIONAL",
        [LL_DEBUG] = "DEBUG",
    };
    sz_h = snprintf(buf, LOG_MSG_CAP, "<%s>%s.%06lu %s[%d]: ", levels[lvl],
                    tm_str, d_us.count(), name, pid);
  }

  // Write the body into the buffer
  ssize_t const cap =
      LOG_MSG_CAP - sz_h - 2; // Room for optional newline and \0
  sz = vsnprintf(&buf[sz_h], cap, format, args);

  if (sz > cap) {
    sz = cap;
  }
  sz += sz_h;

  // Some consumers expect newline-delimited logs.
  if (log_ctx.mode != LOG_SYSLOG) {
    buf[sz] = '\n';
    buf[sz + 1] = '\0';
    sz++;
  }

  // Flush to file descriptor
  do {
    if (log_ctx.mode == LOG_SYSLOG) {
      rc = sendto(log_ctx.fd, buf, sz, MSG_NOSIGNAL, nullptr, 0);
    } else {
      rc = write(log_ctx.fd, buf, sz);
    }
  } while (rc < 0 && errno == EINTR);
}

// NOLINTNEXTLINE(cert-dcl50-cpp)
void olprintfln(int lvl, int fac, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlprintfln(lvl, fac, fmt, args);
  va_end(args);
}

// NOLINTNEXTLINE(cert-dcl50-cpp)
void lprintfln(int lvl, int fac, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlprintfln(lvl, fac, fmt, args);
  va_end(args);
}

void LOG_set_logs_allowed_function(LogsAllowedCallback logs_allowed_function) {
  log_ctx.logs_allowed_function = std::move(logs_allowed_function);
}

bool LOG_is_logging_enabled_for_level(int level) {
  return (level <= log_ctx.level) &&
      (!log_ctx.logs_allowed_function || log_ctx.logs_allowed_function()) &&
      (!log_ctx.rate_limiter || log_ctx.rate_limiter->check());
}

} // namespace ddprof
