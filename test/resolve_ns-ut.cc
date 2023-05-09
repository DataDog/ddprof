// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "device_utils.hpp"

TEST(ResolveNS, device_number_split) {
  {
    unsigned int major_dev_num = custom_major(3145862);
    unsigned int minor_dev_num = custom_minor(3145862);
    printf("%u:%u \n", major_dev_num, minor_dev_num);
    EXPECT_EQ(major_dev_num, 0);
    EXPECT_EQ(minor_dev_num, 902);
  }

}
