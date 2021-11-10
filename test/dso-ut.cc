// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_hdr.hpp"

#include <gtest/gtest.h>
#include <string>

#include "loghandle.hpp"
namespace ddprof {

Dso build_dso_5_1500() { return Dso(5, 1500, 1999, 10, "foo.so.1"); }

Dso build_dso_10_1000() { return Dso(10, 1000, 1200, 0, "bar.so.1"); }

Dso build_dso_10_1000_dupe() { return Dso(10, 1000, 1500); }

Dso build_dso_10_2000() { return Dso(10, 2000, 2500); }

Dso build_dso_10_1500() { return Dso(10, 1500, 1999); }

Dso build_dso_vdso() { return Dso(10, 12, 13, 14, "[vdso]/usr/var/12"); }

Dso build_dso_vsyscall() { return Dso(0, 0, 0, 7, "[vsyscall]/some/syscall"); }

Dso build_dso_file_10_2500() {
  std::string fileName = IPC_TEST_DATA "/dso_test_data.so";
  return Dso(10, 2501, 2510, 0, std::move(fileName));
}

/*
 PID 5
 <1500----1999>

 PID 10
 <1000----1200> 1300 <1500----1999> <2000----2500>
                  ^
 Example looking for 1300 with lower bound should give us the element just after
 that.
*/

void fill_mock_dsoset(DsoSet &set) {
  auto insert_res = set.insert(build_dso_5_1500());
  EXPECT_TRUE(insert_res.first->_type == dso::kStandard);
  EXPECT_TRUE(insert_res.second);

  insert_res = set.insert(build_dso_10_1000());
  EXPECT_TRUE(insert_res.second);

  // insert with equal key (start and pid)
  insert_res = set.insert(build_dso_10_1000_dupe());
  EXPECT_FALSE(insert_res.second);

  EXPECT_EQ(insert_res.first->_start, 1000);
  insert_res = set.insert(build_dso_10_2000());
  EXPECT_TRUE(insert_res.second);

  insert_res = set.insert(build_dso_10_1500());
  EXPECT_TRUE(insert_res.second);
  // empty --> undef
  EXPECT_TRUE(insert_res.first->_type == dso::kAnon);
}

void fill_mock_hdr(DsoHdr &dso_hdr) { fill_mock_dsoset(dso_hdr._set); }

TEST(DSOTest, InsertAndFind) {
  DsoSet set;
  fill_mock_dsoset(set);
  for (const auto &el : set) {
    std::cerr << el;
  }
  std::cerr << "Find dso 10 1000 " << std::endl;
  // First higher than dso 1 (should be dso )
  Dso dso_10_1000 = build_dso_10_1000();
  {
    const auto it = set.find(dso_10_1000);
    EXPECT_TRUE(it != set.end());
  }
  {
    const auto it = set.lower_bound(dso_10_1000);
    std::cerr << *it;
    // should be itself
    EXPECT_EQ(it->_start, 1000);
  }
}

TEST(DSOTest, is_within) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  DsoFindRes find_res = dso_hdr.dso_find_closest(10, 1300);
  EXPECT_FALSE(find_res.second);
  ASSERT_FALSE(find_res.first == dso_hdr._set.end());
  EXPECT_EQ(find_res.first->_pid, 10);
  EXPECT_EQ(find_res.first->_start, 1000);
}

// caught bug in implem
TEST(DSOTest, is_within_2) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  DsoFindRes find_res = dso_hdr.dso_find_closest(10, 2300);
  EXPECT_EQ(find_res.second, true);
}

TEST(DSOTest, intersections) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  {
    Dso dso_inter(10, 900, 1700);
    DsoRange range = dso_hdr.get_intersection(dso_inter);
    EXPECT_EQ(range.first->_pid, 10);
    EXPECT_EQ(range.first->_start, 1000);
    // contains the 1500 -> 1999 element, WARNING the end element is after the
    // intersection
    EXPECT_EQ(range.second->_start, 2000);
    EXPECT_EQ(range.second->_end, 2500);
  }
  {
    Dso dso_no(10, 400, 500);
    DsoRange range = dso_hdr.get_intersection(dso_no);
    EXPECT_EQ(range.first, dso_hdr._set.end());
    EXPECT_EQ(range.second, dso_hdr._set.end());
  }
  {
    Dso dso_other_pid(9, 900, 1700);
    DsoRange range = dso_hdr.get_intersection(dso_other_pid);
    EXPECT_EQ(range.first, dso_hdr._set.end());
    EXPECT_EQ(range.second, dso_hdr._set.end());
  }
  { // single element
    Dso dso_equal_addr(10, 1200, 1400);
    DsoRange range = dso_hdr.get_intersection(dso_equal_addr);
    ASSERT_TRUE(range.first != dso_hdr._set.end());
    ASSERT_TRUE(range.second != dso_hdr._set.end());

    EXPECT_EQ(range.first->_start, 1000);
    EXPECT_EQ(range.second->_start, 1500);
  }
}

TEST(DSOTest, erase) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  {
    DsoRange range = dso_hdr.get_pid_range(10);
    EXPECT_TRUE(range.first->same_or_smaller(build_dso_10_1000()));
    EXPECT_EQ(range.second, dso_hdr._set.end());
    dso_hdr.erase_range(range);
    EXPECT_EQ(dso_hdr._set.size(), 1);
  }
}

TEST(DSOTest, find_same) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  {
    Dso dso_equal_addr(10, 1000, 1400);
    DsoFindRes find_res = dso_hdr.dso_find_same_or_smaller(dso_equal_addr);
    EXPECT_FALSE(find_res.second);
    EXPECT_EQ(find_res.first->_start, 1000);
  }
}

/*
 PID 5
 <1500----1999>

 PID 10
 <1000----1200>  <1500----1999> <2000----2500>

 insert :
    <1100 ------------1700>
*/

TEST(DSOTest, insert_erase_overlap) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  {
    {
      Dso dso_overlap(10, 1100, 1700);
      dso_hdr.insert_erase_overlap(std::move(dso_overlap));
    }
    DsoFindRes find_res = dso_hdr.dso_find_same_or_smaller(build_dso_10_1000());
    EXPECT_FALSE(find_res.second);
    find_res = dso_hdr.dso_find_same_or_smaller(build_dso_10_1500());
    EXPECT_FALSE(find_res.second);
    EXPECT_EQ(dso_hdr._set.size(), 3);
    {
      Dso dso_overlap_2(10, 1100, 1700);
      find_res = dso_hdr.dso_find_same_or_smaller(dso_overlap_2);
      EXPECT_TRUE(find_res.second);
    }
  }
}

TEST(DSOTest, path_type) {
  Dso vdso_dso = build_dso_vdso();
  EXPECT_TRUE(vdso_dso._type == dso::kVdso);
  Dso syscall_dso = build_dso_vsyscall();
  EXPECT_TRUE(syscall_dso._type == dso::kVsysCall);
}

TEST(DSOTest, file_dso) {
  DsoHdr dso_hdr;
  DsoFindRes insert_res =
      dso_hdr.insert_erase_overlap(build_dso_file_10_2500());
  EXPECT_TRUE(insert_res.second);
  const Dso &dso = *insert_res.first;
  const ddprof::RegionHolder &region = dso_hdr.find_or_insert_region(dso);
  std::cerr << "Region " << region.get_region() << std::endl;
}

// clang-format off
static const char *s_exec_line = "55d7883a1000-55d7883a5000 r-xp 00002000 fe:01 3287864                    /usr/local/bin/BadBoggleSolver_run";
static const char *s_exec_line2 = "55d788391000-55d7883a1000 r-xp 00002000 fe:01 0                    /usr/local/bin/BadBoggleSolver_run_2";
static const char *s_exec_line3 = "55d788391000-55d7883a1001 r-xp 00002000 fe:01 0                    /usr/local/bin/BadBoggleSolver_run_3";
// same as number 3 though smaller
static const char *s_exec_line4 = "55d788391000-55d7883a1000 r-xp 00002000 fe:01 0                    /usr/local/bin/BadBoggleSolver_run_3";

static const char *s_line_noexec = "7f531437a000-7f531437b000 r--p 00000000 fe:01 3932979                    /usr/lib/x86_64-linux-gnu/ld-2.31.so";
static const char *s_vdso_lib = "7ffcd6ce6000-7ffcd6ce8000 r-xp 00000000 00:00 0                          [vdso]";
static const char *s_stack_line = "7ffcd6c68000-7ffcd6c89000 rw-p 00000000 00:00 0                          [stack]";

// clang-format on

TEST(DSOTest, dso_from_procline) {
  LogHandle loghandle;
  // todo make dso_from_procline const
  Dso no_exec =
      DsoHdr::dso_from_procline(10, const_cast<char *>(s_line_noexec));
  std::cerr << no_exec;
  EXPECT_EQ(no_exec._type, dso::kStandard);
  EXPECT_EQ(no_exec._executable, false);
  EXPECT_EQ(no_exec._pid, 10);

  Dso standard_dso =
      DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line));
  std::cerr << standard_dso;
  EXPECT_EQ(standard_dso._type, dso::kStandard);
  Dso vdso_dso = DsoHdr::dso_from_procline(10, const_cast<char *>(s_vdso_lib));
  std::cerr << vdso_dso;
  EXPECT_EQ(vdso_dso._type, dso::kVdso);
  Dso stack_dso =
      DsoHdr::dso_from_procline(10, const_cast<char *>(s_stack_line));
  std::cerr << stack_dso;
  EXPECT_EQ(stack_dso._type, dso::kStack);

  DsoHdr dso_hdr;
  {
    // check that we don't overlap between lines that end on same byte
    Dso standard_dso_2 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line2));
    std::cerr << standard_dso_2;
    EXPECT_EQ(standard_dso._type, dso::kStandard);
    dso_hdr.insert_erase_overlap(std::move(standard_dso_2));
    dso_hdr.insert_erase_overlap(std::move(standard_dso));
    EXPECT_EQ(dso_hdr._set.size(), 2);
  }
  {
    // check that we erase everything if we have an overlap
    Dso standard_dso_3 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line3));
    std::cerr << standard_dso_3;
    EXPECT_EQ(standard_dso._type, dso::kStandard);
    dso_hdr.insert_erase_overlap(std::move(standard_dso_3));
    EXPECT_EQ(dso_hdr._set.size(), 1);
  }
  {
    // check that we still match element number 3
    Dso standard_dso_4 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line4));
    std::cerr << standard_dso_4;
    DsoFindRes findres =
        dso_hdr.insert_erase_overlap(std::move(standard_dso_4));
    EXPECT_EQ(findres.second, true);
    std::cerr << *findres.first;
    Dso standard_dso_3 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line3));
    // check that 4 did not override number 3 (even if they overlap 3 is
    // smaller)
    EXPECT_EQ(findres.first->_end, standard_dso_3._end);
  }
}

// Retrieves instruction pointer
#define _THIS_IP_                                                              \
  ({                                                                           \
    __label__ __here;                                                          \
  __here:                                                                      \
    (unsigned long)&&__here;                                                   \
  })

// backpopulate on this unit test making sure we find the associated dso
TEST(DSOTest, backpopulate) {
  LogHandle loghandle;
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
  ASSERT_TRUE(find_res.second);
  // check that string dso-ut is contained in the dso
  EXPECT_TRUE(find_res.first->_filename.find(MYNAME) != std::string::npos);
  dso_hdr._set.erase(find_res.first);
  find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
  EXPECT_FALSE(find_res.second);
}
} // namespace ddprof
