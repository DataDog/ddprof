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

#include "ratelimiter.hpp"

namespace ddprof {

bool IntervalRateLimiter::check_slow() noexcept {
  auto now = CoarseMonotonicClock::now();
  auto interval_end = _interval_end.load(std::memory_order_acquire);
  if (now < interval_end) {
    return false;
  }

  if (!_interval_end.compare_exchange_weak(interval_end, now + _interval,
                                           std::memory_order_release)) {
    // We raced with another thread that reset the timestamp.
    // We treat this as if we fell into the previous interval, and so we
    // rate-limit ourself.
    return false;
  }

  if (interval_end == CoarseMonotonicClock::time_point{}) {
    // If we initialized timestamp_ for the very first time increment count_ by
    // one instead of setting it to 0.  Our original increment made it roll over
    // to 0, so other threads may have already incremented it again and passed
    // the check.
    auto old_count = _count.fetch_add(1, std::memory_order_acq_rel);
    // Check to see if other threads already hit the rate limit cap before we
    // finished checkSlow().
    return old_count < _max_count_per_interval;
  }

  _count.store(1, std::memory_order_release);
  return true;
}

} // namespace ddprof
