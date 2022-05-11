// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent_lib.h"
#include "ringbuffer_holder.hpp"
#include "ringbuffer_utils.hpp"

#include <gtest/gtest.h>

using namespace ddprof;

TEST(ringbuffer, full) {
  const size_t buf_size_order = 1;
  RingBufferHolder ring_buffer{buf_size_order};
  RingBufferWriter writer{ring_buffer.get_ring_buffer()};
  RingBufferReader reader{ring_buffer.get_ring_buffer()};
  EXPECT_EQ(reader.available_for_read(), 0);

  auto sz = writer.available_size();
  EXPECT_GT(sz, 0);
  auto buffer = writer.reserve(sz);
  std::fill(buffer.begin(), buffer.end(), std::byte{0xff});

  EXPECT_EQ(writer.available_size(), 0);
  writer.commit();

  reader.check_for_new_data();
  EXPECT_EQ(reader.available_for_read(), sz);
  auto buffer2 = reader.read_all_available();
  EXPECT_EQ(reader.available_for_read(), 0);
  EXPECT_TRUE(
      std::equal(buffer.begin(), buffer.end(), buffer2.begin(), buffer2.end()));
}
