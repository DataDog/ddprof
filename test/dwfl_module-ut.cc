// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <gtest/gtest.h>

#include "ddprof_module.hpp"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_symbol.hpp"
#include "loghandle.hpp"

#include "dwfl_symbol_lookup.hpp"
#include "symbol_table.hpp"

#include <filesystem>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {

// Retrieves instruction pointer
#define _THIS_IP_                                                              \
  ({                                                                           \
    __label__ __here;                                                          \
  __here:                                                                      \
    (unsigned long)&&__here;                                                   \
  })

namespace fs = std::filesystem;

int count_fds(pid_t pid) {
  std::string proc_dir = "/proc/" + std::to_string(pid) + "/fd";
  int fd_count = 0;
  for ([[maybe_unused]] const auto &entry : fs::directory_iterator(proc_dir)) {
    ++fd_count;
  }
  return fd_count;
}
#ifdef TEMP_REMOVE
TEST(DwflModule, inconsistency_test) {
  pid_t my_pid = getpid();
  int nb_fds_start = count_fds(my_pid);
  printf("-- Start open file descriptors: %d\n", nb_fds_start);
  LogHandle handle;
  // Load DSOs from our unit test
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  DsoHdr::DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(my_pid, ip);
  // Check that we found the DSO matching this IP
  ASSERT_TRUE(find_res.second);
  {
    DwflWrapper dwfl_wrapper;
    // retrieve the map associated to pid
    DsoHdr::DsoMap &dso_map = dso_hdr.get_pid_mapping(my_pid)._map;

    for (auto it = dso_map.begin(); it != dso_map.end(); ++it) {
      Dso &dso = it->second;
      if (!has_relevant_path(dso._type) || !dso.is_executable()) {
        continue; // skip non exec / non standard (anon/vdso...)
      }

      FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
      ASSERT_TRUE(file_info_id > k_file_info_error);

      const FileInfoValue &file_info_value =
          dso_hdr.get_file_info_value(file_info_id);
      DDProfMod *ddprof_mod = nullptr;
      auto res = dwfl_wrapper.register_mod(dso._start,
                                           dso_hdr.get_elf_range(dso_map, it),
                                           file_info_value, &ddprof_mod);

      ASSERT_TRUE(IsDDResOK(res));
      ASSERT_TRUE(ddprof_mod->_mod);
      if (find_res.first == it) {
        Symbol symbol;
        GElf_Sym elf_sym;
        Offset_t bias;
        EXPECT_TRUE(
            symbol_get_from_dwfl(ddprof_mod->_mod, ip, symbol, elf_sym, bias));
        EXPECT_EQ("ddprof::DwflModule_inconsistency_test_Test::TestBody()",
                  symbol._demangle_name);
        EXPECT_EQ(bias, ddprof_mod->_sym_bias);
        FileAddress_t elf_addr = ip - ddprof_mod->_sym_bias;
        FileAddress_t start_sym, end_sym = {};
        EXPECT_TRUE(compute_elf_range(elf_addr, elf_sym, start_sym, end_sym));
        printf("Start --> 0x%lx - end %lx - bias 0x%lx <--\n", start_sym,
               end_sym, bias);
        EXPECT_GE(elf_addr, start_sym);
        EXPECT_LE(elf_addr, end_sym);

        // Only expect build-id on this binary (as we can not force it on
        // others)
        EXPECT_FALSE(ddprof_mod->_build_id.empty());
      }
      // check that we loaded all mods matching the DSOs
      EXPECT_EQ(ddprof_mod->_status, DDProfMod::kUnknown);
    }
  }
  int nb_fds_end = count_fds(my_pid);
  printf("-- End open file descriptors: %d\n", nb_fds_end);
  EXPECT_EQ(nb_fds_start, nb_fds_end);
}

TEST(DwflModule, short_lived) {
  // Here we are testing that short lived forks don't keep the reference of
  // first file we encounter. By accessing the file through /proc we can fail to
  // access the same file for later pids.
  LogHandle handle;
  // Load DSOs from our unit test
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;

  pid_t child_pid = fork();
  if (child_pid == 0) {
    // First child process
    sleep(1);
    exit(0);
  }
  // Parse the first pid
  dso_hdr.dso_find_or_backpopulate(child_pid, ip);

  {
    DwflWrapper dwfl_wrapper;
    // retrieve the map associated to pid
    DsoHdr::DsoMap &dso_map = dso_hdr.get_pid_mapping(child_pid)._map;

    for (auto it = dso_map.begin(); it != dso_map.end(); ++it) {
      Dso &dso = it->second;
      if (!has_relevant_path(dso._type) || !dso.is_executable()) {
        continue; // skip non exec / non standard (anon/vdso...)
      }

      FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
      ASSERT_TRUE(file_info_id > k_file_info_error);

      const FileInfoValue &file_info_value =
          dso_hdr.get_file_info_value(file_info_id);
      DDProfMod *ddprof_mod = nullptr;
      auto res = dwfl_wrapper.register_mod(dso._start,
                                           dso_hdr.get_elf_range(dso_map, it),
                                           file_info_value, &ddprof_mod);
      ASSERT_TRUE(IsDDResOK(res));
      ASSERT_TRUE(ddprof_mod->_mod);
    }
  }
  // Wait for the first PID to die
  waitpid(child_pid, nullptr, 0);

  pid_t second_child_pid = fork();
  if (second_child_pid == 0) {
    // Second child process
    sleep(1);
    exit(0);
  }

  // Parse the second pid
  dso_hdr.dso_find_or_backpopulate(second_child_pid, ip);
  {
    DwflWrapper dwfl_wrapper;
    // retrieve the map associated to pid
    DsoHdr::DsoMap &dso_map = dso_hdr.get_pid_mapping(second_child_pid)._map;

    for (auto it = dso_map.begin(); it != dso_map.end(); ++it) {
      Dso &dso = it->second;
      if (!has_relevant_path(dso._type) || !dso.is_executable()) {
        continue; // skip non exec / non standard (anon/vdso...)
      }

      FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
      ASSERT_TRUE(file_info_id > k_file_info_error);

      const FileInfoValue &file_info_value =
          dso_hdr.get_file_info_value(file_info_id);
      DDProfMod *ddprof_mod = nullptr;
      auto res = dwfl_wrapper.register_mod(dso._start,
                                           dso_hdr.get_elf_range(dso_map, it),
                                           file_info_value, &ddprof_mod);
      ASSERT_TRUE(IsDDResOK(res));
      ASSERT_TRUE(ddprof_mod->_mod);
    }
  }
}
#endif
__attribute__((always_inline)) inline ElfAddress_t deeper_function() {
  // Without these instructions we can fall in the calling function
  LG_DBG("Adding some logging instructions");
  ElfAddress_t ip = _THIS_IP_;
  LG_DBG("I'm capturing the ip = %lx", ip);
  return ip;
}

__attribute__((always_inline)) inline ElfAddress_t inlined_function() {
  LG_DBG("Before the call to deeper func!!");
  ElfAddress_t ip = deeper_function();
  LG_DBG("I'm going up!!");
  return ip;
}

ElfAddress_t my_custom_function() {
  ElfAddress_t ip = inlined_function();
  LG_DBG("The actual ip = %lx", ip);
  return ip;
}

TEST(DwflModule, inlined_func) {
  pid_t my_pid = getpid();
  LogHandle handle;
  // Load DSOs from our unit test
  ElfAddress_t ip = my_custom_function();
  DsoHdr dso_hdr;
  ddprof::SymbolTable table;
  DwflSymbolLookup symbol_lookup;
  DsoSymbolLookup dso_lookup;
  DsoHdr::DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(my_pid, ip);
  // Check that we found the DSO matching this IP
  ASSERT_TRUE(find_res.second);
  {
    DwflWrapper dwfl_wrapper;
    // retrieve the map associated to pid
    DsoHdr::DsoMap &dso_map = dso_hdr.get_pid_mapping(my_pid)._map;
    for (auto it = dso_map.begin(); it != dso_map.end(); ++it) {
      Dso &dso = it->second;
      if (!has_relevant_path(dso._type) || !dso.is_executable()) {
        continue; // skip non exec / non standard (anon/vdso...)
      }
      FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
      ASSERT_TRUE(file_info_id > k_file_info_error);

      const FileInfoValue &file_info_value =
          dso_hdr.get_file_info_value(file_info_id);
      DDProfMod *ddprof_mod = nullptr;
      auto res = dwfl_wrapper.register_mod(dso._start,
                                           dso_hdr.get_elf_range(dso_map, it),
                                           file_info_value, &ddprof_mod);

      ASSERT_TRUE(IsDDResOK(res));
      ASSERT_TRUE(ddprof_mod->_mod);
      if (find_res.first == it) {
        std::vector<SymbolIdx_t> symbol_indices;
        symbol_lookup.get_or_insert(dwfl_wrapper._dwfl, *ddprof_mod, table,
                                    dso_lookup, file_info_id, ip, dso,
                                    symbol_indices);
        const auto &sym = table[symbol_indices[0]];
        LG_DBG("Sym = %s", sym._demangle_name.c_str());
        EXPECT_EQ(sym._demangle_name, "deeper_function");
      }
    }
  }
}
} // namespace ddprof
