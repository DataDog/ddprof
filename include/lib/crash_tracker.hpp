#pragma once

#include "ddres_def.hpp"
#include "pevent.hpp"

#include <mutex>

namespace ddprof {

DDRes install_signal_handler(void);

class CrashTracker {
public:
  explicit CrashTracker(PEvent *pevent) : _pevent(pevent) {}
  CrashTracker(const CrashTracker &) = delete;
  CrashTracker &operator=(const CrashTracker &) = delete;

  static DDRes crash_tracking_init(PEvent *pevent);
  static void crash_tracking_free();
  static void track_crash(int signal_type);

private:
  struct TrackerState {
    std::mutex _mutex;
  };

  static CrashTracker *create_instance(PEvent *pevent);

  DDRes push_sample(int signal_type);

  TrackerState _state;
  PEvent *_pevent;

  static CrashTracker *_instance;
};

} // namespace ddprof