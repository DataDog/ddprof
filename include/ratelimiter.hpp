/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Taken from facebook/folly
#pragma once

#include "clocks.hpp"

#include <atomic>
#include <chrono>

namespace ddprof {

/**
 * A rate limiter that can rate limit events to N events per M milliseconds.
 *
 * It is intended to be fast to check when messages are not being rate limited.
 * When messages are being rate limited it is slightly slower, as it has to
 * check the clock each time check() is called in this case.
 */
class IntervalRateLimiter {
public:
  IntervalRateLimiter(uint64_t max_count_per_interval,
                      std::chrono::nanoseconds interval) noexcept
      : _max_count_per_interval(max_count_per_interval), _interval(interval) {}

  bool check() {
    auto old_count = _count.fetch_add(1, std::memory_order_acq_rel);
    if (old_count < _max_count_per_interval) {
      return true;
    }
    return check_slow();
  }

private:
  bool check_slow() noexcept;

  uint64_t _max_count_per_interval;
  std::chrono::nanoseconds _interval;
  // Initialize count_ to the maximum possible value so that the first
  // call to check() will call checkSlow() to initialize timestamp_,
  // but subsequent calls will hit the fast-path and avoid checkSlow()
  std::atomic<uint64_t> _count{std::numeric_limits<uint64_t>::max()};
  std::atomic<CoarseMonotonicClock::time_point> _interval_end;
};

} // namespace ddprof
