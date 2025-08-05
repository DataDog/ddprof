// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_hdr.hpp"

#include <gtest/gtest.h>
#include <pthread.h>
#include <string>
#include <sys/mman.h>

#include "defer.hpp"
#include "loghandle.hpp"
#include "perf_clock.hpp"
#include "user_override.hpp"

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
  std::string fileName = UNIT_TEST_DATA "/dso_test_data.so";
  // not using the current pid would fail (as we need to access the file in the
  // context of the process)
  return Dso(getpid(), 2501, 2510, 0, std::move(fileName));
}

/*
 PID 5
 <1500----1999>

 PID 10
 <1000----1199> 1300 <1500----1999> <2000----2500>
                  ^
 Example looking for 1300 with lower bound should give us the element just after
 that.
*/

void fill_mock_hdr(DsoHdr &dso_hdr) {
  auto insert_res = dso_hdr.insert_erase_overlap(build_dso_5_1500());
  EXPECT_TRUE(insert_res.first->second._type == DsoType::kStandard);
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
  EXPECT_TRUE(insert_res.first->second._type == DsoType::kAnon);
}

TEST(DSOTest, is_within) {
  DsoHdr dso_hdr;
  fill_mock_hdr(dso_hdr);
  DsoFindRes find_res = dso_hdr.dso_find_closest(10, 1300);
  EXPECT_TRUE(find_res.second);
  std::string dso_str = find_res.first->second.to_string();
  EXPECT_EQ(dso_str, "PID[10] 3e8-5db 0 (bar.so.1)(T-Standard)(--x)(ID#-1)");
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
    DsoRange range =
        dso_hdr.get_intersection(dso_hdr.get_pid_mapping(10)._map, dso_inter);
    EXPECT_EQ(range.first->second._pid, 10);
    EXPECT_EQ(range.first->second._start, 1000);
    // contains the 1500 -> 1999 element, WARNING the end element is after the
    // intersection
    EXPECT_EQ(range.second->second._start, 2000);
    EXPECT_EQ(range.second->second._end, 2500);
  }
  {
    Dso dso_no(10, 400, 500);
    DsoRange range = dso_hdr.get_intersection(10, dso_no);
    EXPECT_EQ(range.first, range.second);
  }
  {
    Dso dso_other_pid(9, 900, 1700);
    DsoRange range = dso_hdr.get_intersection(9, dso_other_pid);
    EXPECT_EQ(range.first, range.second);
  }
  { // single element
    Dso dso_equal_addr(10, 1200, 1400);
    DsoRange range = dso_hdr.get_intersection(10, dso_equal_addr);
    DsoHdr::DsoFindRes not_found = dso_hdr.find_res_not_found(10);
    ASSERT_TRUE(range.first != not_found.first);
    ASSERT_TRUE(range.second != not_found.first);
    EXPECT_EQ(range.first->second._start, 1000);
    EXPECT_EQ(range.second->second._start, 1500);
  }
  {
    Dso dso_inter(10, 1500, 1999);
    DsoRange range = dso_hdr.get_intersection(10, dso_inter);
    EXPECT_EQ(range.first->second._pid, 10);
    EXPECT_EQ(range.first->second._start, 1500);
    EXPECT_EQ(range.second->second._start, 2000);
  }
  {
    Dso dso_inter(10, 1499, 2000);
    DsoRange range = dso_hdr.get_intersection(10, dso_inter);
    EXPECT_EQ(range.first->second._pid, 10);
    EXPECT_EQ(range.first->second._start, 1000);
    EXPECT_EQ(range.second, dso_hdr.get_pid_mapping(10)._map.end());
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
    DsoFindRes find_res = dso_hdr.dso_find_adjust_same(
        dso_hdr.get_pid_mapping(10)._map, dso_equal_addr);
    ASSERT_FALSE(find_res.second);
    EXPECT_EQ(find_res.first->second._start, 1000);
  }
}

/*
 PID 5
 <1500----1999>

 PID 10
 <1000----1199>  <1500----1999> <2000----2500>

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
    DsoFindRes find_res = dso_hdr.dso_find_adjust_same(
        dso_hdr.get_pid_mapping(10)._map, build_dso_10_1000());
    EXPECT_FALSE(find_res.second);
    find_res = dso_hdr.dso_find_adjust_same(dso_hdr.get_pid_mapping(10)._map,
                                            build_dso_10_1500());
    EXPECT_FALSE(find_res.second);
    EXPECT_EQ(dso_hdr.get_nb_dso(), 4);
    {
      Dso dso_overlap_2(10, 1100, 1700);
      find_res = dso_hdr.dso_find_adjust_same(dso_hdr.get_pid_mapping(10)._map,
                                              dso_overlap_2);
      EXPECT_TRUE(find_res.second);
    }
  }
}

TEST(DSOTest, path_type) {
  Dso vdso_dso = build_dso_vdso();
  EXPECT_TRUE(vdso_dso._type == DsoType::kVdso);
  Dso syscall_dso = build_dso_vsyscall();
  EXPECT_TRUE(syscall_dso._type == DsoType::kVsysCall);
}

// clang-format off
static const char *const s_exec_line = "55d7883a1000-55d7883a5000 r-xp 00002000 fe:01 3287864                    /usr/local/bin/BadBoggleSolver_run";
static const char *const s_exec_line2 = "55d788391000-55d7883a1000 r-xp 00002000 fe:01 0                    /usr/local/bin/BadBoggleSolver_run_2";
static const char *const s_exec_line3 = "55d788391000-55d7883a1001 r-xp 00002000 fe:01 0                    /usr/local/bin/BadBoggleSolver_run_3";
// same as number 3 though smaller
static const char *const s_exec_line4 = "55d788391000-55d7883a1000 r-xp 00002000 fe:01 0                    /usr/local/bin/BadBoggleSolver_run_3";

static const char *const s_line_noexec = "7f531437a000-7f531437b000 r--p 00000000 fe:01 3932979                    /usr/lib/x86_64-linux-gnu/ld-2.31.so";
static const char *const s_vdso_lib = "7ffcd6ce6000-7ffcd6ce8000 r-xp 00000000 00:00 0                          [vdso]";
static const char *const s_stack_line = "7ffcd6c68000-7ffcd6c89000 rw-p 00000000 00:00 0                          [stack]";
static const char *const s_inode_line = "7ffcd6c89000-7ffcd6c92000 rw-p 00000000 00:00 0                          anon_inode:[perf_event]";

static const char *const s_jsa_line = "0x800000000-0x800001fff rw-p 00000000 00:00 0                          /usr/local/openjdk-11/lib/server/classes.jsa";

static const char *const s_dd_profiling = "0x800000000-0x800001fff rw-p 00000000 00:00 0                          /tmp/libdd_profiling.so.1234";
static const char *const s_dotnet_line = "7fbd4f1e4000-7fbd4f1ec000 r--s 00000000 ca:01 140372                     /usr/share/dotnet/shared/Microsoft.NETCore.App/6.0.5/System.Runtime.dll";
static const char *const s_jitdump_line = "7b5242e44000-7b5242e45000 r-xp 00000000 fd:06 22295230                   /home/r1viollet/.debug/jit/llvm-IR-jit-20230131-981d92/jit-3237589.dump";

static const char *const s_empty_file_line = "7f9b650b1000-7f9b650b4000 rw-p 00000000 00:00 0                 \n";
static const char *const s_bad_line = "7b5242e44000-7b5242e45000 r-xp  00000000 fd:06";

// clang-format on

TEST(DSOTest, dso_from_proc_line) {
  LogHandle handle;
  Dso no_exec = DsoHdr::dso_from_proc_line(10, s_line_noexec);
  EXPECT_EQ(no_exec._type, DsoType::kStandard);
  EXPECT_EQ(no_exec._prot, PROT_READ);
  EXPECT_EQ(no_exec._pid, 10);
  Dso standard_dso = DsoHdr::dso_from_proc_line(10, s_exec_line);
  { // standard
    EXPECT_EQ(standard_dso._type, DsoType::kStandard);
  }
  { // vdso
    Dso vdso_dso = DsoHdr::dso_from_proc_line(10, s_vdso_lib);
    EXPECT_EQ(vdso_dso._type, DsoType::kVdso);
  }
  { // stack
    Dso stack_dso = DsoHdr::dso_from_proc_line(10, s_stack_line);
    EXPECT_EQ(stack_dso._type, DsoType::kStack);
  }
  { // inode
    Dso inode_dso = DsoHdr::dso_from_proc_line(10, s_inode_line);
    EXPECT_EQ(inode_dso._type, DsoType::kAnon);
  }
  {
    // jsa
    Dso jsa_dso = DsoHdr::dso_from_proc_line(10, s_jsa_line);
    EXPECT_EQ(jsa_dso._type, DsoType::kRuntime);
  }
  {
    // dotnet dll
    Dso dll_dso = DsoHdr::dso_from_proc_line(10, s_dotnet_line);
    EXPECT_EQ(dll_dso._type, DsoType::kRuntime);
  }
  DsoHdr dso_hdr;
  {
    // check that we don't overlap between lines that end on same byte
    Dso standard_dso_2 = DsoHdr::dso_from_proc_line(10, s_exec_line2);
    EXPECT_EQ(standard_dso._type, DsoType::kStandard);
    dso_hdr.insert_erase_overlap(std::move(standard_dso_2));
    dso_hdr.insert_erase_overlap(std::move(standard_dso));
    EXPECT_EQ(dso_hdr.get_nb_dso(), 2);
  }
  {
    // check that we erase everything if we have an overlap
    Dso standard_dso_3 = DsoHdr::dso_from_proc_line(10, s_exec_line3);
    EXPECT_EQ(standard_dso._type, DsoType::kStandard);
    dso_hdr.insert_erase_overlap(std::move(standard_dso_3));
    EXPECT_EQ(dso_hdr.get_nb_dso(), 1);
  }
  {
    // check that we still match element number 3
    Dso standard_dso_4 = DsoHdr::dso_from_proc_line(10, s_exec_line4);
    ProcessAddress_t end_4 = standard_dso_4._end;
    DsoFindRes findres =
        dso_hdr.insert_erase_overlap(std::move(standard_dso_4));
    EXPECT_EQ(findres.first->second._end, end_4);
  }
  {
    Dso dd_profiling_dso = DsoHdr::dso_from_proc_line(10, s_dd_profiling);
    EXPECT_EQ(dd_profiling_dso._type, DsoType::kDDProfiling);
  }
  {
    Dso jitdump_dso = DsoHdr::dso_from_proc_line(3237589, s_jitdump_line);
    EXPECT_EQ(jitdump_dso._type, DsoType::kJITDump);
  }
  { // jitdump with a name different from PID (for whole host)
    Dso jitdump_dso = DsoHdr::dso_from_proc_line(12, s_jitdump_line);
    EXPECT_EQ(jitdump_dso._type, DsoType::kJITDump);
  }
  {
    // empty file proc line
    Dso dso = DsoHdr::dso_from_proc_line(12, s_empty_file_line);
    EXPECT_EQ(dso._type, DsoType::kAnon);
  }
  {
    // bad proc line
    Dso dso = DsoHdr::dso_from_proc_line(12, s_bad_line);
    EXPECT_EQ(dso._pid, -1);
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
  std::string filename_disk =
      file_info._path.substr(file_info._path.find_last_of("/") + 1);

  std::string filename_procfs = find_res.first->second._filename.substr(
      find_res.first->second._filename.find_last_of("/") + 1);

  EXPECT_EQ(filename_procfs, filename_disk);
  // manually erase the unit test's binary
  dso_hdr.get_pid_mapping(getpid())._map.erase(find_res.first);
  find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
  EXPECT_TRUE(find_res.second);
}

TEST(DSOTest, backpopulate_with_perf_clock) {
  ElfAddress_t ip = _THIS_IP_;

  // PerfClock will return 0
  PerfClock::reset();
  {
    DsoHdr dso_hdr;
    auto old_timestamp = PerfClock::now();
    DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
    ASSERT_TRUE(find_res.second);

    auto &my_dso = find_res.first->second;
    bool result1 = dso_hdr.maybe_insert_erase_overlap(Dso{my_dso, getpid()},
                                                      old_timestamp);
    EXPECT_TRUE(result1);
  }
  // Init perf clock
  PerfClock::init();
  {
    DsoHdr dso_hdr;
    auto old_timestamp = PerfClock::now();
    DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
    ASSERT_TRUE(find_res.second);

    auto &my_dso = find_res.first->second;
    auto current_timestamp = PerfClock::now();
    bool result2 = dso_hdr.maybe_insert_erase_overlap(Dso{my_dso, getpid()},
                                                      old_timestamp);
    EXPECT_FALSE(result2);

    bool result3 = dso_hdr.maybe_insert_erase_overlap(Dso{my_dso, getpid()},
                                                      PerfClock::now());
    EXPECT_TRUE(result3);
  }
}

TEST(DSOTest, missing_dso) {
  DsoHdr dso_hdr;
  // Build fake dso
  Dso foo_dso = build_dso_5_1500();
  FileInfo file_info = dso_hdr.find_file_info(foo_dso);
  EXPECT_TRUE(file_info._path.empty());
  EXPECT_FALSE(file_info._inode);
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
  pid_t my_pid = getpid();
  int nb_elts;
  dso_hdr.pid_backpopulate(my_pid, nb_elts);
  EXPECT_TRUE(nb_elts);
  DsoHdr::PidMapping &pid_mapping = dso_hdr.get_pid_mapping(my_pid);
  bool found = false;
  Dso copy;
  for (auto &el : pid_mapping._map) {
    Dso &dso = el.second;
    // emulate an insert of big size
    if (dso._filename.find("c++") != std::string::npos && dso._offset == 0) {
      copy = dso;
      copy._end = copy._start + 0x388FFF;
      found = true;
    }
  }
  EXPECT_TRUE(found);
  dso_hdr.insert_erase_overlap(pid_mapping, std::move(copy));
  dso_hdr.pid_backpopulate(my_pid, nb_elts);
  // TODO: To be discussed - should we erase overlapping or not
}

TEST(DSOTest, insert_jitdump) {
  // mmap the jitdump file
  DsoHdr dso_hdr;
  // pid from dso line (important for the jitdump name)
  pid_t test_pid = 3237589;
  Dso jitdump_dso = DsoHdr::dso_from_proc_line(test_pid, s_jitdump_line);
  EXPECT_EQ(jitdump_dso._type, DsoType::kJITDump);
  ProcessAddress_t start = jitdump_dso._start;
  DsoHdr::PidMapping &pid_mapping = dso_hdr.get_pid_mapping(test_pid);
  dso_hdr.insert_erase_overlap(pid_mapping, std::move(jitdump_dso));
  EXPECT_EQ(start, pid_mapping._jitdump_addr);
}

TEST(DSOTest, exe_name) {
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(getpid(), ip);
  ASSERT_TRUE(find_res.second);
  pid_t my_pid = getpid();
  std::string exe_name;
  bool found_exe = dso_hdr.find_exe_name(my_pid, exe_name);
  EXPECT_TRUE(found_exe);
  LG_NTC("%s", exe_name.c_str());
}

TEST(DSOTest, user_change) {
  if (!is_root()) {
    return;
  }

  pthread_barrierattr_t bat = {};
  pthread_barrier_t *pb = static_cast<pthread_barrier_t *>(
      mmap(NULL, sizeof(pthread_barrier_t), PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  pthread_barrierattr_init(&bat);
  pthread_barrierattr_setpshared(&bat, 1);
  pthread_barrier_init(pb, &bat, 2);
  defer { munmap(pb, sizeof(*pb)); };

  if (pid_t child_pid = fork(); child_pid > 0) {
    defer {
      pthread_barrier_wait(pb);
      waitpid(child_pid, NULL, 0);
    };
    DsoHdr dso_hdr;
    pthread_barrier_wait(pb);
    int nb_elts;
    ASSERT_TRUE(dso_hdr.pid_backpopulate(child_pid, nb_elts));
  } else {
    ASSERT_TRUE(IsDDResOK(become_user("nobody")));
    pthread_barrier_wait(pb);
    pthread_barrier_wait(pb);
  }
}

TEST(DSOTest, large_backpopulate) {
  // This is a test of the same java application one minute apart
  // This can be useful to bench the backpopulate
  std::string path_to_proc = std::string(UNIT_TEST_DATA) + "/dso-ut/step-1";
  DsoHdr dso_hdr(path_to_proc);
  int elts_added;
  dso_hdr.pid_backpopulate(2, elts_added);
  path_to_proc = std::string(UNIT_TEST_DATA) + "/dso-ut/step-2";
  ASSERT_EQ(dso_hdr.get_nb_dso(), 1759);
  ASSERT_EQ(dso_hdr.get_nb_dso(), elts_added);
  dso_hdr.reset_backpopulate_state(0);
  dso_hdr.set_path_to_proc(path_to_proc);
  dso_hdr.pid_backpopulate(2, elts_added);
  // check that there is no growth
  ASSERT_EQ(dso_hdr.get_nb_dso(), 1759);
}

TEST(DSOTest, elf_load_simple) {
  DsoHdr dso_hdr;
  Dso dso1{5, 0x1000, 0x4fff, 0, "libfoo.so.1", 0, PROT_READ};
  // map whole file
  dso_hdr.insert_erase_overlap(Dso{dso1});

  // map second segment
  Dso dso2{5, 0x2000, 0x4fff, 0x1000, "libfoo.so.1", 0, PROT_READ | PROT_EXEC};
  dso_hdr.insert_erase_overlap(Dso{dso2});

  ASSERT_EQ(dso_hdr.get_nb_dso(), 2);

  auto [it1, found1] = dso_hdr.dso_find_closest(5, 0x1000);
  ASSERT_TRUE(found1);
  ASSERT_EQ(it1->first, 0x1000);
  ASSERT_TRUE(dso1.is_same_or_smaller(it1->second));
  ASSERT_EQ(it1->second.end(), 0x1fff);

  auto [it2, found2] = dso_hdr.dso_find_closest(5, 0x2000);
  ASSERT_TRUE(found2);
  ASSERT_EQ(it2->first, 0x2000);
  ASSERT_EQ(it2->second, dso2);
}

TEST(DSOTest, elf_load) {
  DsoHdr dso_hdr;
  const Dso dso1{5, 0x1000, 0x5fff, 0, "libfoo.so.1", 0, PROT_READ};
  // map whole file
  dso_hdr.insert_erase_overlap(Dso{dso1});

  // map 2nd segment
  const Dso dso2{
      5, 0x2000, 0x3fff, 0x1000, "libfoo.so.1", 0, PROT_READ | PROT_EXEC};
  dso_hdr.insert_erase_overlap(Dso{dso2});

  ASSERT_EQ(dso_hdr.get_nb_dso(), 3);

  {
    auto [it1, found1] = dso_hdr.dso_find_closest(5, 0x1000);
    ASSERT_TRUE(found1);
    ASSERT_EQ(it1->first, 0x1000);
    ASSERT_TRUE(dso1.is_same_or_smaller(it1->second));
    ASSERT_EQ(it1->second.end(), 0x1fff);

    auto [it2, found2] = dso_hdr.dso_find_closest(5, 0x2000);
    ASSERT_TRUE(found2);
    ASSERT_EQ(it2->first, 0x2000);
    ASSERT_EQ(it2->second, dso2);

    Dso dso1_right{dso1};
    dso1_right.adjust_start(0x4000);
    auto [it3, found3] = dso_hdr.dso_find_closest(5, 0x4000);
    ASSERT_TRUE(found3);
    ASSERT_EQ(it3->first, 0x4000);
    ASSERT_EQ(it3->second, dso1_right);
  }

  // map 3rd segment
  const Dso dso3{
      5, 0x4000, 0x4fff, 0x2000, "libfoo.so.1", 0, PROT_READ | PROT_WRITE};
  dso_hdr.insert_erase_overlap(Dso{dso3});

  ASSERT_EQ(dso_hdr.get_nb_dso(), 4);

  {
    auto [it1, found1] = dso_hdr.dso_find_closest(5, 0x1000);
    ASSERT_TRUE(found1);
    ASSERT_EQ(it1->first, 0x1000);
    ASSERT_TRUE(dso1.is_same_or_smaller(it1->second));
    ASSERT_EQ(it1->second.end(), 0x1fff);

    auto [it2, found2] = dso_hdr.dso_find_closest(5, 0x2000);
    ASSERT_TRUE(found2);
    ASSERT_EQ(it2->first, 0x2000);
    ASSERT_EQ(it2->second, dso2);

    auto [it3, found3] = dso_hdr.dso_find_closest(5, 0x4000);
    ASSERT_TRUE(found3);
    ASSERT_EQ(it3->first, 0x4000);
    ASSERT_EQ(it3->second, dso3);

    Dso dso1_right{dso1};
    dso1_right.adjust_start(0x5000);
    auto [it4, found4] = dso_hdr.dso_find_closest(5, 0x5000);
    ASSERT_TRUE(found3);
    ASSERT_EQ(it4->first, 0x5000);
    ASSERT_EQ(it4->second, dso1_right);
  }

  // anonymous mapping at the end
  const Dso dso4{5, 0x5000, 0x5fff};
  dso_hdr.insert_erase_overlap(Dso{dso4});

  ASSERT_EQ(dso_hdr.get_nb_dso(), 4);

  {
    auto [it1, found1] = dso_hdr.dso_find_closest(5, 0x1000);
    ASSERT_TRUE(found1);
    ASSERT_EQ(it1->first, 0x1000);
    ASSERT_TRUE(dso1.is_same_or_smaller(it1->second));
    ASSERT_EQ(it1->second.end(), 0x1fff);

    auto [it2, found2] = dso_hdr.dso_find_closest(5, 0x2000);
    ASSERT_TRUE(found2);
    ASSERT_EQ(it2->first, 0x2000);
    ASSERT_EQ(it2->second, dso2);

    auto [it3, found3] = dso_hdr.dso_find_closest(5, 0x4000);
    ASSERT_TRUE(found3);
    ASSERT_EQ(it3->first, 0x4000);
    ASSERT_EQ(it3->second, dso3);

    auto [it4, found4] = dso_hdr.dso_find_closest(5, 0x5000);
    ASSERT_TRUE(found3);
    ASSERT_EQ(it4->first, 0x5000);
    ASSERT_EQ(it4->second, dso4);
  }
}

TEST(DSOTest, elf_load_single_segment) {
  DsoHdr dso_hdr;
  const Dso dso1{5, 0x1000, 0x5fff, 0, "libfoo.so.1", 0, PROT_READ};
  // map whole file
  dso_hdr.insert_erase_overlap(Dso{dso1});

  // anonymous mapping at the end
  const Dso dso2{5, 0x5000, 0x5fff};
  dso_hdr.insert_erase_overlap(Dso{dso2});

  ASSERT_EQ(dso_hdr.get_nb_dso(), 2);
  {
    auto [it1, found1] = dso_hdr.dso_find_closest(5, 0x4fff);
    ASSERT_TRUE(found1);
    ASSERT_EQ(it1->first, 0x1000);
    ASSERT_TRUE(dso1.is_same_or_smaller(it1->second));
    ASSERT_EQ(it1->second.end(), 0x4fff);

    auto [it2, found2] = dso_hdr.dso_find_closest(5, 0x5000);
    ASSERT_TRUE(found2);
    ASSERT_EQ(it2->first, 0x5000);
    ASSERT_EQ(it2->second, dso2);
  }
}

} // namespace ddprof
