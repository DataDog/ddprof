// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pprof/ddprof_pprof.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddprof_cmdline.hpp"
#include "ddprof_cmdline_watcher.hpp"
#include "loghandle.hpp"
#include "map_utils.hpp"
#include "pevent_lib_mocks.hpp"
#include "symbol_hdr.hpp"
#include "unwind_output_mock.hpp"

#include <datadog/blazesym.h>

#include <cstdlib>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <string>
#include <time.h>
#include <unistd.h>

namespace ddprof {
TEST(DDProfPProf, init_profiles) {
  DDProfPProf pprof;
  DDProfContext ctx = {};
  ctx.watchers.push_back(*ewatcher_from_str("sCPU"));
  DDRes res = pprof_create_profile(&pprof, ctx);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

void test_pprof(DDProfPProf *pprofs) {
  ddog_prof_Profile *profile = &pprofs->_profile;

  struct ddog_prof_Profile_SerializeResult serialized_result =
      ddog_prof_Profile_serialize(profile, nullptr, nullptr);

  ASSERT_EQ(serialized_result.tag, DDOG_PROF_PROFILE_SERIALIZE_RESULT_OK);

  // Get the bytes from the encoded profile using the new API
  auto bytes_result = ddog_prof_EncodedProfile_bytes(&serialized_result.ok);
  ASSERT_EQ(bytes_result.tag, DDOG_PROF_RESULT_BYTE_SLICE_OK_BYTE_SLICE);

  const ddog_ByteSlice *buffer = &bytes_result.ok;

  EXPECT_TRUE(buffer->ptr);

  // Test that we are generating content
  EXPECT_TRUE(buffer->len > 500);

  ddog_prof_EncodedProfile_drop(&serialized_result.ok);
}

TEST(DDProfPProf, aggregate) {
  LogHandle handle;
  SymbolHdr symbol_hdr;
  UnwindOutput mock_output;
  SymbolTable &table = symbol_hdr._symbol_table;
  MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;
  FileInfoVector file_infos;
  fill_unwind_symbols(table, mapinfo_table, mock_output);
  DDProfPProf pprof;
  DDProfContext ctx = {};

  bool ok = watchers_from_str("sCPU", ctx.watchers);
  EXPECT_TRUE(ok);
  DDRes res = pprof_create_profile(&pprof, ctx);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_index != -1);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_count_index != -1);
  res = pprof_aggregate(&mock_output, symbol_hdr, {1000, 1, 0},
                        &ctx.watchers[0], file_infos, false, kSumPos,
                        ctx.worker_ctx.symbolizer, &pprof);

  EXPECT_TRUE(IsDDResOK(res));

  test_pprof(&pprof);

  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

TEST(DDProfPProf, just_live) {
  LogHandle handle;
  SymbolHdr symbol_hdr;
  UnwindOutput mock_output;
  SymbolTable &table = symbol_hdr._symbol_table;
  MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;

  fill_unwind_symbols(table, mapinfo_table, mock_output);
  DDProfPProf pprof;
  DDProfContext ctx = {};
  {
    bool ok = watchers_from_str("sDUM", ctx.watchers);
    EXPECT_TRUE(ok);
  }
  {
    bool ok = watchers_from_str("sALLOC mode=l", ctx.watchers);
    EXPECT_TRUE(ok);
  }
  log_watcher(&(ctx.watchers[0]), 0);
  log_watcher(&(ctx.watchers[1]), 1);

  DDRes res = pprof_create_profile(&pprof, ctx);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_index == -1);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_count_index == -1);

  EXPECT_TRUE(ctx.watchers[1].pprof_indices[kLiveSumPos].pprof_index != -1);
  EXPECT_TRUE(ctx.watchers[1].pprof_indices[kLiveSumPos].pprof_count_index !=
              -1);
  FileInfoVector file_infos;
  res = pprof_aggregate(&mock_output, symbol_hdr, {1000, 1, 0},
                        &ctx.watchers[1], file_infos, false, kLiveSumPos,
                        ctx.worker_ctx.symbolizer, &pprof);
  EXPECT_TRUE(IsDDResOK(res));
  test_pprof(&pprof);
  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

// Test that location addresses are properly grouped based on inlining mode
TEST(DDProfPProf, address_grouping_by_inlining_mode) {
  LogHandle handle;

  // Test with inlining disabled - should use function start address
  {
    MapInfo map_info(0x1000, 0x2000, 0, "/test/binary", BuildIdStr{});
    std::array<ddog_prof_Location, 3> locations;
    unsigned write_index = 0;

    // Mock blaze_sym for same function, different instruction addresses
    blaze_symbolize_code_info code_info{.dir = nullptr,
                                        .file = "/test/source.c",
                                        .line = 42,
                                        .column = 0,
                                        .reserved = {}};

    blaze_sym sym{.name = "test_function",
                  .module = "/test/binary",
                  .addr = 0x1000, // Function start address
                  .offset = 0x50, // Offset from function start
                  .size = 0x100,
                  .code_info = code_info,
                  .inlined_cnt = 0,
                  .inlined = nullptr,
                  .reason = {},
                  .reserved = {}};

    HeterogeneousLookupStringMap<std::string> demangled_names;

    // Write first location (inlining disabled)
    ElfAddress_t addr1 = 0x1050; // Different instruction address
    DDRes res = write_location_blaze(addr1, demangled_names, map_info, sym,
                                     write_index, locations, false);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(write_index, 1u);
    // Should use function start address (sym.addr), not instruction address
    EXPECT_EQ(locations[0].address, 0x1000u);

    // Write second location with different instruction address (inlining
    // disabled)
    ElfAddress_t addr2 = 0x1070; // Different instruction in same function
    sym.offset = 0x70;
    res = write_location_blaze(addr2, demangled_names, map_info, sym,
                               write_index, locations, false);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(write_index, 2u);
    // Should also use function start address
    EXPECT_EQ(locations[1].address, 0x1000u);

    // Verify both locations have same address (grouped)
    EXPECT_EQ(locations[0].address, locations[1].address);
  }

  // Test with inlining enabled - should use instruction address
  {
    MapInfo map_info(0x1000, 0x2000, 0, "/test/binary", BuildIdStr{});
    std::array<ddog_prof_Location, 3> locations;
    unsigned write_index = 0;

    blaze_symbolize_code_info code_info{.dir = nullptr,
                                        .file = "/test/source.c",
                                        .line = 42,
                                        .column = 0,
                                        .reserved = {}};

    blaze_sym sym{.name = "test_function",
                  .module = "/test/binary",
                  .addr = 0x1000,
                  .offset = 0x50,
                  .size = 0x100,
                  .code_info = code_info,
                  .inlined_cnt = 0,
                  .inlined = nullptr,
                  .reason = {},
                  .reserved = {}};

    HeterogeneousLookupStringMap<std::string> demangled_names;

    // Write first location (inlining enabled)
    ElfAddress_t addr1 = 0x1050;
    DDRes res = write_location_blaze(addr1, demangled_names, map_info, sym,
                                     write_index, locations, true);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(write_index, 1u);
    // Should use actual instruction address
    EXPECT_EQ(locations[0].address, 0x1050u);

    // Write second location with different instruction address (inlining
    // enabled)
    ElfAddress_t addr2 = 0x1070;
    sym.offset = 0x70;
    res = write_location_blaze(addr2, demangled_names, map_info, sym,
                               write_index, locations, true);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(write_index, 2u);
    // Should use actual instruction address
    EXPECT_EQ(locations[1].address, 0x1070u);

    // Verify locations have different addresses (not grouped)
    EXPECT_NE(locations[0].address, locations[1].address);
  }
}

} // namespace ddprof
