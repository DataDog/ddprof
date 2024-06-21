// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "crash_tracker.hpp"

#include "dumpable.hpp"
#include "logger.hpp"
#include "syscalls.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstring>
#include <linux/futex.h>
#include <span>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unordered_set>

namespace ddprof {

namespace {

using SignalSpan = std::span<const int>;
using OldActions = std::array<struct sigaction, NSIG>;
using SignalSet = std::unordered_set<int>;
using SignalHandler = void (*)(int, siginfo_t *, void *);

constexpr std::array<int, 10> kFatalSignals = {
    SIGABRT, SIGBUS, SIGFPE,  SIGILL,  SIGQUIT,
    SIGSEGV, SIGSYS, SIGTRAP, SIGXCPU, SIGXFSZ,
};

// convert pid to string without allocation (assuming out has enough space)
void pid_to_string(pid_t pid, std::string *out) {
  constexpr int base = 10;

  if (pid == 0) {
    out->assign("0");
    return;
  }

  int ndigits = 0;
  // count number of digits
  for (pid_t tmp = pid; tmp > 0; tmp /= base) {
    ++ndigits;
  }

  out->resize(ndigits);
  for (int i = ndigits - 1; i >= 0; --i) {
    (*out)[i] = '0' + (pid % base);
    pid /= base;
  }
}

bool install_signal_handler(int sig, SignalHandler handler, int flags,
                            struct sigaction *old_action) {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = flags | SA_SIGINFO;
  sa.sa_sigaction = handler;
  if (sigaction(sig, &sa, old_action) != 0) {
    LG_ERR("Failed to install signal handler for signal %d: %s", sig,
           strerror(errno));
    return false;
  }

  return true;
}

bool install_signal_handlers(SignalSpan signals, SignalHandler handler,
                             int flags = 0, OldActions *old_actions = nullptr,
                             const SignalSet *signals_to_ignore = nullptr) {
  return std::ranges::all_of(signals, [&](int signal) {
    if (signals_to_ignore &&
        signals_to_ignore->find(signal) != signals_to_ignore->end()) {
      return true;
    }
    return install_signal_handler(signal, handler, flags,
                                  old_actions ? &(*old_actions)[signal]
                                              : nullptr);
  });
}

void restore_signal_handler_and_raise_signal(
    siginfo_t *siginfo, const struct sigaction *old_action) {
  bool restore_default_action = old_action == nullptr;

  if (!restore_default_action) {
    if (sigaction(siginfo->si_signo, old_action, nullptr) != 0) {
      restore_default_action = true;
    }
  }
  if (restore_default_action) {
    struct sigaction default_action;
    sigemptyset(&default_action.sa_mask);
    default_action.sa_flags = 0;
    default_action.sa_handler = SIG_DFL;
    if (sigaction(siginfo->si_signo, &default_action, nullptr) != 0) {
      return;
    }
  }

  const int ret =
      rt_tgsigqueueinfo(getpid(), gettid(), siginfo->si_signo, siginfo);
  if (ret == 0) {
    return;
  }

  raise(siginfo->si_signo);
}

class CrashTracker {
public:
  static CrashTracker *instance() {
    static auto *instance = new CrashTracker();
    return instance;
  }

  bool init(std::vector<std::string> argv);

  ~CrashTracker() = delete;
  CrashTracker(const CrashTracker &) = delete;
  CrashTracker &operator=(const CrashTracker &) = delete;

private:
  static void handle_signal(int signo, siginfo_t *siginfo, void *ucontext) {
    auto *tracker = CrashTracker::instance();
    tracker->track_crash(signo, siginfo, ucontext);
  }

  void wait_for_crash_tracking_done() {
    const int timeout_sec = 5;
    timespec timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_nsec = 0;
    futex(&_crash_tracking_done, FUTEX_WAIT_PRIVATE, 0, &timeout, nullptr, 0);
  }

  void wake_crash_tracking_done() {
    _crash_tracking_done = 1;
    futex(&_crash_tracking_done, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr,
          0);
  }

  void track_crash_impl() {
    const pid_t tid = gettid();
    pid_to_string(tid, &_argv.back());

    const DumpableGuard dumpableGuard;
    const pid_t pid = fork();
    if (pid < 0) {
      return;
    }

    if (pid == 0) {
      execv(_cargv[0], _cargv.data());
      _exit(EXIT_FAILURE);
    }

    // \fixme{nsavoire}: race condition here, we should synchronize with the
    // child to make sure set ptracer is done before child does a ptrace attach.
    // We could use a socketpair / pipe to synchronize with the child.
    prctl(PR_SET_PTRACER, pid, 0, 0, 0);

    int status;
    waitpid(pid, &status, 0);
  }

  void track_crash(int signo, siginfo_t *siginfo, void * /*ucontext*/) {
    if (!_crash_tracked.test_and_set()) {
      track_crash_impl();
      wake_crash_tracking_done();
    } else {
      wait_for_crash_tracking_done();
    }

    restore_signal_handler_and_raise_signal(siginfo, &_old_actions[signo]);
  }

  CrashTracker() = default;

  std::vector<std::string> _argv;
  std::vector<char *> _cargv;
  std::array<struct sigaction, NSIG> _old_actions;
  std::atomic_flag _crash_tracked;
  uint32_t _crash_tracking_done = 0;
  bool _initialized = false;
};

bool CrashTracker::init(std::vector<std::string> argv) {
  if (_initialized) {
    return false;
  }

  _argv = std::move(argv);

  _cargv.clear();
  std::ranges::transform(
      _argv, std::back_inserter(_cargv),
      // cppcheck is completely wrong about `arg` that could be declared const
      // cppcheck-suppress constParameterReference
      [](std::string &arg) { return arg.data(); });
  _cargv.push_back(nullptr);

  _initialized = true;

  // \fixme{nsavoire}: set up alternate stack (alternate stack needs to be set
  // up for each thread)
  return install_signal_handlers(kFatalSignals, handle_signal, SA_ONSTACK,
                                 &_old_actions);
}

std::vector<std::string>
get_handler_argv(const std::string &handler_exe,
                 const std::vector<std::string> &handler_args) {
  std::vector<std::string> argv;
  argv.push_back(handler_exe);
  std::copy(handler_args.begin(), handler_args.end(), std::back_inserter(argv));

  argv.emplace_back("--pid");
  // reserve space for tid
  argv.emplace_back(std::numeric_limits<pid_t>::digits10, '0');
  return argv;
}

} // namespace

bool install_crash_tracker(const std::string &handler_exe,
                           const std::vector<std::string> &handler_args) {
  auto *crash_handler = CrashTracker::instance();
  return crash_handler->init(get_handler_argv(handler_exe, handler_args));
}

} // namespace ddprof
