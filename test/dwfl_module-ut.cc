#include "dwfl_module.hpp"

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"

#include <gtest/gtest.h>
#include <string>

#include "dwfl_internals.hpp"
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
    EXPECT_EQ(ddprof_mod._status, DDProfMod::kInconsistent);
  }
}

} // namespace ddprof
