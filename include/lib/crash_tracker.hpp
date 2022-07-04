#pragma once

#include "ddres.hpp"

namespace ddprof {

CrashTracker {
public:
    CrashTracker(const CrashTracker &) = delete;
    CrashTracker &operator=(const CrashTracker &) = delete;

    static DDRes crash_tracking_init(const RingBufferInfo &ring_buffer);
    static void crash_tracking_free();
    static inline __attribute__((no_sanitize("address"))) void

private:
    struct TrackerState {
        std::mutex _mutex;
    }

    static AllocationTracker *create_instance();
    track_crash();

    TrackerState _state;
    PEvent _pevent;
    DDRes init(const RingBufferInfo &ring_buffer);
    void free();

    static CrashTracer *_instance;
};

}
