// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_hdr.hpp"

#include <gtest/gtest.h>
#include <string>

#include "loghandle.hpp"
namespace ddprof {

using DsoFindRes = DsoHdr::DsoFindRes;
using DsoRange = DsoHdr::DsoRange;

// clang-format off
// This will insert following elements 
// <DEBUG>Dec 12 16:20:50 dso-ut[60184]: [DSO] : Insert PID[5] 5dc-7cf a (foo.so.1)(T-Standard)(x)(ID#-1)
// <DEBUG>Dec 12 16:20:50 dso-ut[60184]: [DSO] : Insert PID[10] 3e8-4af 0 (bar.so.1)(T-Standard)(x)(ID#-1)
// <DEBUG>Dec 12 16:20:50 dso-ut[60184]: [DSO] : Insert PID[10] 3e8-5db 0 (bar.so.1)(T-Standard)(x)(ID#-1) <<- override
// <DEBUG>Dec 12 16:20:50 dso-ut[60184]: [DSO] : Insert PID[10] 5dc-7cf 0 ()(T-Anonymous)(x)(ID#-1)
// <DEBUG>Dec 12 16:20:50 dso-ut[60184]: [DSO] : Insert PID[10] 7d0-9c4 0 ()(T-Anonymous)(x)(ID#-1)
// clang-format on

Dso build_dso_5_1500() { return Dso(5, 1500, 1999, 10, "foo.so.1"); }

Dso build_dso_10_1000() { return Dso(10, 1000, 1199, 0, "bar.so.1"); }

Dso build_dso_10_1000_dupe() { return Dso(10, 1000, 1499, 0, "bar.so.1"); }

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

void fill_mock_hdr(DsoHdr &dso_hdr) {
  auto insert_res = dso_hdr.insert_erase_overlap(build_dso_5_1500());
  EXPECT_TRUE(insert_res.first->second._type == dso::kStandard);
  EXPECT_TRUE(insert_res.second);

  insert_res = dso_hdr.insert_erase_overlap(build_dso_10_1000());
  EXPECT_TRUE(insert_res.second);

  // insert with equal key (start and pid)
  insert_res = dso_hdr.insert_erase_overlap(build_dso_10_1000_dupe());
  EXPECT_TRUE(insert_res.second);
  EXPECT_EQ(insert_res.first->second._start, 1000);

  insert_res = dso_hdr.insert_erase_overlap(build_dso_10_2000());
  EXPECT_TRUE(insert_res.second);

  insert_res = dso_hdr.insert_erase_overlap(build_dso_10_1500());
  EXPECT_TRUE(insert_res.second);
  EXPECT_TRUE(insert_res.first->second._type == dso::kAnon);
}

TEST(DSOTest, is_within) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  DsoFindRes find_res = dso_hdr.dso_find_closest(10, 1300);
  EXPECT_TRUE(find_res.second);
  DsoHdr::DsoFindRes not_found = dso_hdr.find_res_not_found(10);
  ASSERT_FALSE(find_res == not_found);
  EXPECT_EQ(find_res.first->second._pid, 10);
  EXPECT_EQ(find_res.first->second._start, 1000);
}

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
    DsoRange range = dso_hdr.get_intersection(dso_hdr._map[10], dso_inter);
    EXPECT_EQ(range.first->second._pid, 10);
    EXPECT_EQ(range.first->second._start, 1000);
    // contains the 1500 -> 1999 element, WARNING the end element is after the
    // intersection
    EXPECT_EQ(range.second->second._start, 2000);
    EXPECT_EQ(range.second->second._end, 2500);
  }
  {
    Dso dso_no(10, 400, 500);
    DsoRange range = dso_hdr.get_intersection(dso_hdr._map[10], dso_no);
    DsoHdr::DsoFindRes not_found = dso_hdr.find_res_not_found(10);
    EXPECT_EQ(range.first, not_found.first);
    EXPECT_EQ(range.second, not_found.first);
  }
  {
    Dso dso_other_pid(9, 900, 1700);
    DsoRange range = dso_hdr.get_intersection(dso_hdr._map[9], dso_other_pid);
    DsoHdr::DsoFindRes not_found = dso_hdr.find_res_not_found(9);
    EXPECT_EQ(range.first, not_found.first);
    EXPECT_EQ(range.second, not_found.first);
  }
  { // single element
    Dso dso_equal_addr(10, 1200, 1400);
    DsoRange range = dso_hdr.get_intersection(dso_hdr._map[10], dso_equal_addr);
    DsoHdr::DsoFindRes not_found = dso_hdr.find_res_not_found(10);
    ASSERT_TRUE(range.first != not_found.first);
    ASSERT_TRUE(range.second != not_found.first);
    EXPECT_EQ(range.first->second._start, 1000);
    EXPECT_EQ(range.second->second._start, 1500);
  }
}

TEST(DSOTest, erase) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  {
    dso_hdr.pid_free(10);
    EXPECT_EQ(dso_hdr.get_nb_dso(), 1);
  }
}

TEST(DSOTest, find_same) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  {
    Dso dso_equal_addr(10, 1000, 1400); // larger
    DsoFindRes find_res =
        dso_hdr.dso_find_adjust_same(dso_hdr._map[10], dso_equal_addr);
    ASSERT_FALSE(find_res.second);
    EXPECT_EQ(find_res.first->second._start, 1000);
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
    DsoFindRes find_res =
        dso_hdr.dso_find_adjust_same(dso_hdr._map[10], build_dso_10_1000());
    EXPECT_FALSE(find_res.second);
    find_res =
        dso_hdr.dso_find_adjust_same(dso_hdr._map[10], build_dso_10_1500());
    EXPECT_FALSE(find_res.second);
    EXPECT_EQ(dso_hdr.get_nb_dso(), 3);
    {
      Dso dso_overlap_2(10, 1100, 1700);
      find_res = dso_hdr.dso_find_adjust_same(dso_hdr._map[10], dso_overlap_2);
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
      dso_hdr.insert_erase_overlap(dso_hdr._map[10], build_dso_file_10_2500());
  ASSERT_TRUE(insert_res.second);
  const Dso &dso = insert_res.first->second;
  const RegionHolder *region = dso_hdr.find_or_insert_region(dso);
  EXPECT_TRUE(region);
  EXPECT_TRUE(region->get_region());
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
static const char *s_inode_line = "7ffcd6c89000-7ffcd6c92000 rw-p 00000000 00:00 0                          anon_inode:[perf_event]";

static const char *s_jsa_line = "0x800000000-0x800001fff rw-p 00000000 00:00 0                          /usr/local/openjdk-11/lib/server/classes.jsa";

// clang-format on

TEST(DSOTest, dso_from_procline) {
  // todo make dso_from_procline const
  Dso no_exec =
      DsoHdr::dso_from_procline(10, const_cast<char *>(s_line_noexec));
  EXPECT_EQ(no_exec._type, dso::kStandard);
  EXPECT_EQ(no_exec._executable, false);
  EXPECT_EQ(no_exec._pid, 10);
  Dso standard_dso =
      DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line));
  { // standard
    EXPECT_EQ(standard_dso._type, dso::kStandard);
  }
  { // vdso
    Dso vdso_dso =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_vdso_lib));
    EXPECT_EQ(vdso_dso._type, dso::kVdso);
  }
  { // stack
    Dso stack_dso =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_stack_line));
    EXPECT_EQ(stack_dso._type, dso::kStack);
  }
  { // inode
    Dso inode_dso =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_inode_line));
    EXPECT_EQ(inode_dso._type, dso::kAnon);
  }
  {
    // jsa
    Dso jsa_dso = DsoHdr::dso_from_procline(10, const_cast<char *>(s_jsa_line));
    EXPECT_EQ(jsa_dso._type, dso::kAnon);
  }
  DsoHdr dso_hdr;
  {
    // check that we don't overlap between lines that end on same byte
    Dso standard_dso_2 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line2));
    EXPECT_EQ(standard_dso._type, dso::kStandard);
    dso_hdr.insert_erase_overlap(std::move(standard_dso_2));
    dso_hdr.insert_erase_overlap(std::move(standard_dso));
    EXPECT_EQ(dso_hdr.get_nb_dso(), 2);
  }
  {
    // check that we erase everything if we have an overlap
    Dso standard_dso_3 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line3));
    EXPECT_EQ(standard_dso._type, dso::kStandard);
    dso_hdr.insert_erase_overlap(std::move(standard_dso_3));
    EXPECT_EQ(dso_hdr.get_nb_dso(), 1);
  }
  {
    // check that we still match element number 3
    Dso standard_dso_4 =
        DsoHdr::dso_from_procline(10, const_cast<char *>(s_exec_line4));
    ProcessAddress_t end_4 = standard_dso_4._end;
    DsoFindRes findres =
        dso_hdr.insert_erase_overlap(std::move(standard_dso_4));
    EXPECT_EQ(findres.first->second._end, end_4);
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
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
  ASSERT_TRUE(find_res.second);
  // check that string dso-ut is contained in the dso
  EXPECT_TRUE(find_res.first->second._filename.find(MYNAME) !=
              std::string::npos);
  // check that we match the local binary
  FileInfo file_info = dso_hdr.find_file_info(find_res.first->second);
  EXPECT_EQ(file_info._path, find_res.first->second._filename);
  // manually erase the unit test's binary
  dso_hdr._map[getpid()].erase(find_res.first);
  find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
  EXPECT_FALSE(find_res.second);
}

TEST(DSOTest, missing_dso) {
  LogHandle loghandle;
  DsoHdr dso_hdr;
  // Build fake dso
  Dso foo_dso = build_dso_5_1500();
  FileInfo file_info = dso_hdr.find_file_info(foo_dso);
  EXPECT_TRUE(file_info._path.empty());
  EXPECT_FALSE(file_info._inode);
}

TEST(DSOTest, pid_read_dso) {
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  pid_t my_pid = getpid();
  dso_hdr.dso_find_or_backpopulate(my_pid, ip);
  const DsoHdr::DsoMap &map = dso_hdr._map[my_pid];
  bool found = false;
  for (auto it = map.begin(); it != map.end(); ++it) {
    const Dso &dso = it->second;
    if (dso._filename.find("c++") != std::string::npos && dso._pgoff == 0) {
      ElfWord_t elf_word = 0;
      DsoHdr::DsoFindRes find_res = dso_hdr.pid_read_dso(
          my_pid, &elf_word, sizeof(ElfWord_t), dso._start + 0x100);
      LG_DBG("Read result = %lx (dso %s)", elf_word, dso.to_string().c_str());
      EXPECT_TRUE(find_res.second);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// clang-format off
// Assuming we get a big insertion
// <DEBUG>Dec 14 14:15:16 ddprof[725]: <0>(MAP)722: /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25 (7f51f1d42000/389000/0)
// <DEBUG>Dec 14 14:15:16 ddprof[725]: [DSO] : Insert PID[722] 7f51f1d42000-7f51f20cafff 0 (/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25)(T-Standard)(x)(ID#-1)
// 
// Followed by following updates 
// <DEBUG>Dec 14 14:15:21 ddprof[725]: [DSO] : Insert PID[722] 7f51f1ebb000-7f51f20bafff 179000 (/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25)(T-Standard)(-)(ID#-1)
// <DEBUG>Dec 14 14:15:21 ddprof[725]: [DSO] : Insert PID[722] 7f51f20bb000-7f51f20c4fff 179000 (/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25)(T-Standard)(-)(ID#-1)
// <DEBUG>Dec 14 14:15:21 ddprof[725]: [DSO] : Insert PID[722] 7f51f20c5000-7f51f20c6fff 183000 (/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25)(T-Standard)(-)(ID#-1)
// why the hell do we have 2 regions, same offset, same memory
// clang-format on

TEST(DSOTest, mmap_into_backpop) {
  DsoHdr dso_hdr;
  // LogHandle log_handle;
  pid_t my_pid = getpid();
  int nb_elts;
  dso_hdr.pid_backpopulate(my_pid, nb_elts);
  EXPECT_TRUE(nb_elts);
  DsoHdr::DsoMap &map = dso_hdr._map[my_pid];
  bool found = false;
  for (auto &el : map) {
    Dso &dso = el.second;
    // emulate an insert of big size
    if (dso._filename.find("c++") != std::string::npos && dso._pgoff == 0) {
      Dso copy(dso);
      copy._end = copy._start + 0x388FFF;
      found = true;
      dso_hdr.insert_erase_overlap(map, std::move(copy));
    }
  }
  EXPECT_TRUE(found);

  dso_hdr.pid_backpopulate(my_pid, nb_elts);
  // TODO: To be discussed - should we erase overlaping or not
}

} // namespace ddprof
