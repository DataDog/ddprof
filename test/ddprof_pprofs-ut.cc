extern "C" {
#include "pprofs/ddprof_pprofs.h"

#include "perf_option.h"
#include "unwind_output.h"
#include <fcntl.h>
#include <unistd.h>
}
#include "loghandle.hpp"

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

// Warning : use with care.
#define SLICE_LITERAL(str)                                                     \
  (struct string_view) { .ptr = (str), sizeof(str) - 1 }

#define SLICE_STRLEN(str)                                                      \
  (struct string_view) { .ptr = (str), strlen(str) }

#define K_MOCK_LOC_SIZE 11
static const char *s_func_names[K_MOCK_LOC_SIZE] = {
    "foo0", "foo1", "foo2", "foo3", "foo4", "foo5",
    "foo6", "foo7", "foo8", "foo9", "foo10"};
static const char *s_src_paths[K_MOCK_LOC_SIZE] = {
    "/app/0/bar.c", "/app/1/bar.c", "/app/2/bar.c", "/app/3/bar.c",
    "/app/4/bar.c", "/app/5/bar.c", "/app/6/bar.c", "/app/7/bar.c",
    "/app/8/bar.c", "/app/9/bar.c", "/app/10/bar.c"};

static const char *s_so_paths[] = {"/app/lib/bar.0.so"};

// ddprof_ffi_Mapping

void fill_unwind_output_1(UnwindOutput *uw_output) {

  uw_output_clear(uw_output);
  uw_output->idx = K_MOCK_LOC_SIZE;

  FunLoc *locs = uw_output->locs;
  for (unsigned i = 0; i < uw_output->idx; ++i) {
    locs[i].funname = SLICE_STRLEN(s_func_names[i]);
    locs[i].srcpath = SLICE_STRLEN(s_src_paths[i]);
    locs[i].sopath = SLICE_STRLEN(s_so_paths[0]);
    locs[i].ip = 42 + i;
    locs[i].map_start = 100 + i;
    locs[i].map_end = 200 + i;
    locs[i].map_off = 300 + i;
    locs[i].line = 10 * i;
    locs[i].disc = 1 + i;
  }
}

TEST(DDProfPProfs, init_profiles) {
  DDProfPProfs pprofs;
  pprofs_init(&pprofs);
  const PerfOption *perf_option_cpu = perfoptions_preset(10);
  DDRes res = pprofs_create_profile(&pprofs, perf_option_cpu, 1);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprofs_free_profile(&pprofs);
  EXPECT_TRUE(IsDDResOK(res));
}

TEST(DDProfPProfs, aggregate) {
  LogHandle handle;
  UnwindOutput mock_output;
  fill_unwind_output_1(&mock_output);
  DDProfPProfs pprofs;
  pprofs_init(&pprofs);
  const PerfOption *perf_option_cpu = perfoptions_preset(10);
  DDRes res = pprofs_create_profile(&pprofs, perf_option_cpu, 1);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprofs_aggregate(&mock_output, 1000, 0, &pprofs);
  EXPECT_TRUE(IsDDResOK(res));

  std::string fileName = IPC_TEST_DATA "/pprof_unit_test.txt";

  int fileFd = open(fileName.c_str(), O_CREAT | O_RDWR, 0600);
  EXPECT_TRUE(fileFd != -1);

  res = ddprof_write_profile(&pprofs, fileFd);
  EXPECT_TRUE(IsDDResOK(res));
  close(fileFd);

  res = pprofs_free_profile(&pprofs);
  EXPECT_TRUE(IsDDResOK(res));
}
