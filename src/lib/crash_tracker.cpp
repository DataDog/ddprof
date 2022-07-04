#include "crash_tracker.hpp"

#include "ddres.hpp"
#include "ringbuffer_utils.hpp"
#include "perf.hpp"

#warning shared writter --> issue ?
#warning share the rentry guard

namespace ddprof {

struct CrashTrackerEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
  uint64_t period;
  uint64_t abi; /* if PERF_SAMPLE_REGS_USER */
  uint64_t regs[PERF_REGS_COUNT];
  /* if PERF_SAMPLE_REGS_USER */
  uint64_t size;                          /* if PERF_SAMPLE_STACK_USER */
  std::byte data[PERF_SAMPLE_STACK_SIZE]; /* if PERF_SAMPLE_STACK_USER */
  uint64_t dyn_size;                      /* if PERF_SAMPLE_STACK_USER &&
                                        size != 0 */
};

CrashTracker *CrashTracker::create_instance() {
  static CrashTracker tracker;
  return &tracker;
}

static void handle_signal(int) {
  ddprof::CrashTracker::track_crash();
}


DDRes install_signal_handler(const RingBufferInfo &ring_buffer) {
  sigset_t sigset;
  struct sigaction sa;
  DDRES_CHECK_ERRNO(sigemptyset(&sigset), DD_WHAT_EXCEPTION_HANDLER,
                    "sigemptyset failed");
  sa.sa_handler = &handle_signal;
  sa.sa_mask = sigset;
  sa.sa_flags = SA_RESTART;
  DDRES_CHECK_ERRNO(sigaction(SIGTERM, &sa, NULL), DD_WHAT_MAINLOOP_INIT,
                    "Setting SIGTERM handler failed");
  DDRES_CHECK_ERRNO(sigaction(SIGINT, &sa, NULL), DD_WHAT_MAINLOOP_INIT,
                    "Setting SIGINT handler failed");
  return {};

}


DDRes CrashTracker::init(const RingBufferInfo &ring_buffer){
  _instance = create_instance();
  DDRES_CHECK_FWD(install_signal_handler(ring_buffer));
}

void CrashTracker::track_crash(){
  std::lock_guard lock{_state._mutex};
  /// send things
}

}