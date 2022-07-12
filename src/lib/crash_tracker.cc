#include "crash_tracker.hpp"

#include "ddres.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "ringbuffer_utils.hpp"
#include "savecontext.hpp"
#include "syscalls.hpp"

#warning shared writter --> issue ?
#warning share the rentry guard

namespace ddprof {

CrashTracker *CrashTracker::_instance;

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

CrashTracker *CrashTracker::create_instance(PEvent *pevent) {
  static CrashTracker tracker(pevent);
  return &tracker;
}

static void handle_signal(int signal_type) {
  ddprof::CrashTracker::track_crash(signal_type);
}

DDRes install_signal_handler(void) {
  sigset_t sigset;
  struct sigaction sa;
  DDRES_CHECK_ERRNO(sigemptyset(&sigset), DD_WHAT_CRASH_TRACKER,
                    "sigemptyset failed");
  sa.sa_handler = &handle_signal;
  sa.sa_mask = sigset;
  sa.sa_flags = SA_RESTART;
#warning what signals should we catch ?

  // DDRES_CHECK_ERRNO(sigaction(SIGTERM, &sa, NULL), DD_WHAT_CRASH_TRACKER,
  //                   "Setting SIGTERM handler failed");
  DDRES_CHECK_ERRNO(sigaction(SIGSEGV, &sa, NULL), DD_WHAT_CRASH_TRACKER,
                    "Setting SIGSEGV handler failed");

  // DDRES_CHECK_ERRNO(sigaction(SIGINT, &sa, NULL), DD_WHAT_MAINLOOP_INIT,
  //                   "Setting SIGINT handler failed");
  return ddres_init();
}

DDRes CrashTracker::crash_tracking_init(PEvent *pevent) {
  _instance = create_instance(pevent);
  DDRES_CHECK_FWD(install_signal_handler());
  return ddres_init();
}

void CrashTracker::crash_tracking_free() {
  if (_instance) {
    _instance = nullptr;
#warning delete signal handler -> keep old behaviour ?
    signal(SIGTERM, SIG_DFL);
  }
}

void CrashTracker::track_crash(int signal_type) {
  if (_instance) {
    if (IsDDResNotOK(_instance->push_sample(signal_type))) {
      LG_DBG("Error pushing sample");
    }
  }
}

DDRes CrashTracker::push_sample(int signal_type) {
#warning what to do with signal type
  std::lock_guard lock{_state._mutex};

  LG_DBG("yay pushing samples %d", signal_type);
  RingBufferWriter writer{_pevent->rb};

  auto needed_size = sizeof(CrashTrackerEvent);

  if (writer.available_size() < needed_size) {
#warning empty ring buffer
    return {};
  }

  Buffer buf = writer.reserve(sizeof(CrashTrackerEvent));
  CrashTrackerEvent *event = reinterpret_cast<CrashTrackerEvent *>(buf.data());
  event->hdr.misc = 0;
  event->hdr.size = sizeof(CrashTrackerEvent);
  event->hdr.type = PERF_RECORD_SAMPLE;
  event->abi = PERF_SAMPLE_REGS_ABI_64;
  event->sample_id.time = 0;

  event->sample_id.pid = getpid();
  event->sample_id.tid = ddprof::gettid();
  // warning hacky
  event->period = signal_type;
  event->size = PERF_SAMPLE_STACK_SIZE;

  event->dyn_size =
      save_context(event->regs, ddprof::Buffer{event->data, event->size});

  if (writer.commit()) {
    uint64_t count = 1;
    if (write(_pevent->fd, &count, sizeof(count)) != sizeof(count)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                             "Error writing to crash eventfd (%s)",
                             strerror(errno));
    }
  }

  return ddres_init();
}

} // namespace ddprof
