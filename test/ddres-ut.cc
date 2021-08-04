#include <gtest/gtest.h>

#include "ddres.h"

#include <iostream>

extern "C" {
#include <stdarg.h>
}

namespace ddprof {

class LogHandle {
public:
  LogHandle() {
    LOG_open(LOG_STDERR, nullptr);
    LOG_setlevel(LL_DEBUG);
  }
  ~LogHandle() { LOG_close(); }
};

TEST(DDRes, Size) {
  DDRes ddres = {0};
  ASSERT_TRUE(sizeof(ddres) == sizeof(int64_t));
}

TEST(DDRes, InitOK) {
  {
    DDRes ddres1 = {0};
    DDRes ddres2 = ddres_init();
    DDRes ddres3;
    InitDDResOK(ddres3);

    ASSERT_TRUE(ddres_equal(ddres1, ddres2));
    ASSERT_TRUE(ddres_equal(ddres1, ddres3));

    ASSERT_FALSE(IsDDResNotOK(ddres2));
    ASSERT_TRUE(IsDDResOK(ddres2));
  }
}

extern "C" {
static int s_call_counter = 0;

DDRes mock_fatal_generator() {
  ++s_call_counter;
  RETURN_FATAL_LOG(DD_LOC_UNITTEST, DD_WHAT_UNITTEST,
                   "Test the log and return function %d", 42);
}

DDRes dderr_wrapper() {
  DDERR_CHECK_FWD(mock_fatal_generator());
  return ddres_init();
}
}

TEST(DDRes, FillFatal) {
  {
    DDRes ddres = ddres_fatal(DD_LOC_UKNW, DD_WHAT_UNITTEST);
    ASSERT_TRUE(IsDDResNotOK(ddres));
    ASSERT_TRUE(IsDDResFatal(ddres));
  }
  {
    LogHandle handle;
    {
      DDRes ddres = mock_fatal_generator();
      ASSERT_TRUE(
          ddres_equal(ddres, ddres_fatal(DD_LOC_UNITTEST, DD_WHAT_UNITTEST)));
    }
    EXPECT_EQ(s_call_counter, 1);

    {
      DDRes ddres = dderr_wrapper();
      ASSERT_TRUE(
          ddres_equal(ddres, ddres_fatal(DD_LOC_UNITTEST, DD_WHAT_UNITTEST)));
    }
    EXPECT_EQ(s_call_counter, 2);
  }
}

void mock_except1() {
  throw DDException(DD_SEVERROR, DD_LOC_UNITTEST, DD_WHAT_UNITTEST);
}

void mock_except2() { throw std::bad_alloc(); }

DDRes mock_wrapper(int idx) {
  try {
    if (idx == 1) {
      mock_except1();
    } else if (idx == 2) {
      mock_except2();
    }
  }
  CatchExcept2DDRes(DD_LOC_UNITTEST);
  return ddres_init();
}

// Check that an exception can be caught and converted back to a C result
TEST(DDRes, ConvertException) {
  LogHandle handle;
  try {
    DDRes ddres = mock_wrapper(1);
    ASSERT_EQ(ddres,
              ddres_create(DD_SEVERROR, DD_LOC_UNITTEST, DD_WHAT_UNITTEST));
    ddres = mock_wrapper(2);
    ASSERT_EQ(ddres,
              ddres_create(DD_SEVERROR, DD_LOC_UNITTEST, DD_WHAT_BADALLOC));

  } catch (...) { ASSERT_TRUE(false); }
}
} // namespace ddprof
