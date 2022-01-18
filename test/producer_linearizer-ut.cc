// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "producer_linearizer.h"
}

#include <gtest/gtest.h>

TEST(ProducerLinearizerTest, SimpleTest) {
  ProducerLinearizer pl = {0};
  // Initializer
  ASSERT_TRUE(ProducerLinearizer_init(&pl, 10));

  // Push some values
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 2, 3));
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 4, 1));
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 6, 2));

  // Pop the values
  uint64_t i;
  ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
  ASSERT_EQ(i, 4);
  ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
  ASSERT_EQ(i, 6);
  ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
  ASSERT_EQ(i, 2);

  // There are no more values, so this should fail
  ASSERT_FALSE(ProducerLinearizer_pop(&pl, &i));

  // Now check double-pushing to a previously pushed, but popped, value
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 2, 3));
  ASSERT_FALSE(ProducerLinearizer_push(&pl, 2, 3));
  ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
  ASSERT_EQ(*pl.I, 2);
  ASSERT_EQ(i, 2);

  // Now mess everything up by changing all values and pushing everything
  for (int j = 0; j < 10; j++)
    ASSERT_TRUE(ProducerLinearizer_push(&pl, j, 100 - j));

  // Now check that the order is correct
  for (int j = 9; j >= 0; j--) {
    ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
    ASSERT_EQ(i, j);
  }

  // We're done here
  ProducerLinearizer_free(&pl);
}
