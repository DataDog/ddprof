#include "dwfl_module.hpp"

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"

#include <gtest/gtest.h>
#include <string>

#include "ddprof_module.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_symbol.hpp"
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

  for (auto it = dso_map.begin(); it != dso_map.end(); ++it) {
    Dso &dso = it->second;
    if (dso._type != dso::kStandard || !dso._executable) {
      continue; // skip non exec / non standard (anon/vdso...)
    }

    FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
    ASSERT_TRUE(file_info_id > k_file_info_error);

    const FileInfoValue &file_info_value =
        dso_hdr.get_file_info_value(file_info_id);
    DDProfModRange mod_range;
    EXPECT_TRUE(IsDDResOK(
        dso_hdr.mod_range_or_backpopulate(find_res.first, dso_map, mod_range)));
    DDProfMod *ddprof_mod =
        dwfl_wrapper.register_mod(dso._start, dso, mod_range, file_info_value);
    EXPECT_TRUE(ddprof_mod->_mod);
    if (find_res.first == it) {
      Symbol symbol;
      GElf_Sym elf_sym;
      Offset_t lbiais;
      bool res =
          symbol_get_from_dwfl(ddprof_mod->_mod, ip, symbol, elf_sym, lbiais);
      EXPECT_TRUE(res);
      EXPECT_EQ("ddprof::DwflModule_inconsistency_test_Test::TestBody()",
                symbol._demangle_name);
      EXPECT_EQ(lbiais, ddprof_mod->_sym_bias);
      FileAddress_t elf_addr = ip - ddprof_mod->_sym_bias;
      FileAddress_t start_sym, end_sym = {};
      res = compute_elf_range(elf_addr, elf_sym, start_sym, end_sym);
      EXPECT_TRUE(res);
      printf("Start --> 0x%lx - end %lx - lbiais 0x%lx <--\n", start_sym,
             end_sym, lbiais);
      EXPECT_GE(elf_addr, start_sym);
      EXPECT_LE(elf_addr, end_sym);
    }
    // check that we loaded all mods matching the DSOs
    EXPECT_EQ(ddprof_mod->_low_addr, mod_range._low_addr);
    EXPECT_EQ(ddprof_mod->_status, DDProfMod::kUnknown);
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

    DDProfModRange bad_range = {._low_addr = bad_dso._start,
                                ._high_addr = bad_dso._end + 1};

    DDProfMod *ddprof_mod = dwfl_wrapper.register_mod(
        bad_dso._start, bad_dso, bad_range, file_info_value);

    EXPECT_EQ(ddprof_mod, nullptr);
    EXPECT_EQ(dwfl_wrapper._inconsistent, true);
  }
}

} // namespace ddprof
