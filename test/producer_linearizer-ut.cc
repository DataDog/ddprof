extern "C" {
#include "producer_linearizer.h"
}

#include <gtest/gtest.h>

TEST(ProducerLinearizerTest, SimpleTest) {
  ProducerLinearizer pl = {0};
  uint64_t MyVals[10] = {0};

  // Initializer
  ASSERT_TRUE(ProducerLinearizer_init(&pl, 10, MyVals));

  // Push some values
  MyVals[2] = 3;
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 2));
  MyVals[4] = 1;
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 4));
  MyVals[6] = 2;
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 6));

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
  ASSERT_TRUE(ProducerLinearizer_push(&pl, 2));
  ASSERT_FALSE(ProducerLinearizer_push(&pl, 2));
  ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
  ASSERT_EQ(*pl.I, 2);
  ASSERT_EQ(i, 2);

  // Now mess everything up by changing all values and pushing everything
  for (int j = 0; j < 10; j++) {
    MyVals[j] = 100 - j;
    ASSERT_TRUE(ProducerLinearizer_push(&pl, j));
  }

  // Now check that the order is correct
  for (int j = 9; j >= 0; j--) {
    ASSERT_TRUE(ProducerLinearizer_pop(&pl, &i));
    ASSERT_EQ(i, j);
  }

  // We're done here
  ProducerLinearizer_free(&pl);
}
