#include "dwfl_module.hpp"

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"

#include <gtest/gtest.h>
#include <string>

#include "dwfl_internals.h"
#include "loghandle.hpp"

namespace ddprof {

// Retrieves instruction pointer
#define _THIS_IP_                                                              \
  ({                                                                           \
    __label__ __here;                                                          \
  __here:                                                                      \
    (unsigned long)&&__here;                                                   \
  })

TEST(DwflModule, inconsistency_test) {
  LogHandle handle;
  // Load DSOs from our unit test
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  pid_t my_pid = getpid();
  DsoHdr::DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(my_pid, ip);
  // Check that we found the DSO matching this IP
  ASSERT_TRUE(find_res.second);

  DwflWrapper dwfl_wrapper;
  // retrieve the map associated to pid
  DsoHdr::DsoMap &dso_map = dso_hdr._map[my_pid];

  for (auto &tmp_dso : dso_map) {
    const Dso &dso = tmp_dso.second;
    if (dso._type != dso::kStandard || !dso._executable) {
      continue; // skip non exec / non standard (anon/vdso...)
    }

    FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
    ASSERT_TRUE(file_info_id > k_file_info_error);

    const FileInfoValue &file_info_value =
        dso_hdr.get_file_info_value(file_info_id);
    DDProfMod ddprof_mod =
        update_module(dwfl_wrapper._dwfl, dso._start, dso, file_info_value);
    // check that we loaded all mods matching the DSOs
    EXPECT_EQ(ddprof_mod._low_addr, dso._start - dso._pgoff);
  }

  {
    // attempt loading a bad DSO that overlap with existing
    // Create a DSO from the unit test DSO
    const Dso &ut_dso = find_res.first->second;
    Dso bad_dso(ut_dso);
    bad_dso._id = k_file_info_undef;
    // Test file to avoid matching file names
    bad_dso._filename = DWFL_TEST_DATA "/dso_test_data.so";
    bad_dso._start += 1; // offset to avoid matching previous dso

    FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(bad_dso);

    const FileInfoValue &file_info_value =
        dso_hdr.get_file_info_value(file_info_id);

    DDProfMod ddprof_mod = update_module(dwfl_wrapper._dwfl, bad_dso._start,
                                         bad_dso, file_info_value);
    EXPECT_EQ(ddprof_mod._low_addr, 0);
    EXPECT_EQ(ddprof_mod._inconsistent, true);
  }
}

// clang-format off
/*
Example of proc maps on a ubuntu 18
00400000-0057f000 r-xp 00000000 fe:01 3935372                            /app/build_UB18_gcc_Rel/test/dwfl_module-ut
0077f000-00780000 r--p 0017f000 fe:01 3935372                            /app/build_UB18_gcc_Rel/test/dwfl_module-ut
00780000-00782000 rw-p 00180000 fe:01 3935372                            /app/build_UB18_gcc_Rel/test/dwfl_module-ut
02478000-02499000 rw-p 00000000 00:00 0                                  [heap]
7f2ba2bb0000-7f2ba2d97000 r-xp 00000000 fe:01 4981142                    /lib/x86_64-linux-gnu/libc-2.27.so
7f2ba2d97000-7f2ba2f97000 ---p 001e7000 fe:01 4981142                    /lib/x86_64-linux-gnu/libc-2.27.so
7f2ba2f97000-7f2ba2f9b000 r--p 001e7000 fe:01 4981142                    /lib/x86_64-linux-gnu/libc-2.27.so
7f2ba2f9b000-7f2ba2f9d000 rw-p 001eb000 fe:01 4981142                    /lib/x86_64-linux-gnu/libc-2.27.so
7f2ba2f9d000-7f2ba2fa1000 rw-p 00000000 00:00 0 
7f2ba2fa1000-7f2ba2fbb000 r-xp 00000000 fe:01 4981203                    /lib/x86_64-linux-gnu/libpthread-2.27.so
7f2ba2fbb000-7f2ba31ba000 ---p 0001a000 fe:01 4981203                    /lib/x86_64-linux-gnu/libpthread-2.27.so
7f2ba31ba000-7f2ba31bb000 r--p 00019000 fe:01 4981203                    /lib/x86_64-linux-gnu/libpthread-2.27.so
7f2ba31bb000-7f2ba31bc000 rw-p 0001a000 fe:01 4981203                    /lib/x86_64-linux-gnu/libpthread-2.27.so
7f2ba31bc000-7f2ba31c0000 rw-p 00000000 00:00 0 
7f2ba31c0000-7f2ba31d7000 r-xp 00000000 fe:01 4981160                    /lib/x86_64-linux-gnu/libgcc_s.so.1
7f2ba31d7000-7f2ba33d6000 ---p 00017000 fe:01 4981160                    /lib/x86_64-linux-gnu/libgcc_s.so.1
7f2ba33d6000-7f2ba33d7000 r--p 00016000 fe:01 4981160                    /lib/x86_64-linux-gnu/libgcc_s.so.1
7f2ba33d7000-7f2ba33d8000 rw-p 00017000 fe:01 4981160                    /lib/x86_64-linux-gnu/libgcc_s.so.1
7f2ba33d8000-7f2ba3575000 r-xp 00000000 fe:01 4981167                    /lib/x86_64-linux-gnu/libm-2.27.so
7f2ba3575000-7f2ba3774000 ---p 0019d000 fe:01 4981167                    /lib/x86_64-linux-gnu/libm-2.27.so
7f2ba3774000-7f2ba3775000 r--p 0019c000 fe:01 4981167                    /lib/x86_64-linux-gnu/libm-2.27.so
7f2ba3775000-7f2ba3776000 rw-p 0019d000 fe:01 4981167                    /lib/x86_64-linux-gnu/libm-2.27.so
7f2ba3776000-7f2ba38ef000 r-xp 00000000 fe:01 4981944                    /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25
7f2ba38ef000-7f2ba3aef000 ---p 00179000 fe:01 4981944                    /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25
7f2ba3aef000-7f2ba3af9000 r--p 00179000 fe:01 4981944                    /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25
7f2ba3af9000-7f2ba3afb000 rw-p 00183000 fe:01 4981944                    /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25
7f2ba3afb000-7f2ba3aff000 rw-p 00000000 00:00 0 
7f2ba3aff000-7f2ba3b28000 r-xp 00000000 fe:01 4981124                    /lib/x86_64-linux-gnu/ld-2.27.so
7f2ba3d18000-7f2ba3d20000 rw-p 00000000 00:00 0 
7f2ba3d28000-7f2ba3d29000 r--p 00029000 fe:01 4981124                    /lib/x86_64-linux-gnu/ld-2.27.so
7f2ba3d29000-7f2ba3d2a000 rw-p 0002a000 fe:01 4981124                    /lib/x86_64-linux-gnu/ld-2.27.so
7f2ba3d2a000-7f2ba3d2b000 rw-p 00000000 00:00 0 
7fffe0efa000-7fffe0f1b000 rw-p 00000000 00:00 0                          [stack]
7fffe0f47000-7fffe0f4b000 r--p 00000000 00:00 0                          [vvar]
7fffe0f4b000-7fffe0f4d000 r-xp 00000000 00:00 0                          [vdso]
ffffffffff600000-ffffffffff601000 r-xp 00000000 00:00 0                  [vsyscall]
*/

// clang-format on

} // namespace ddprof
