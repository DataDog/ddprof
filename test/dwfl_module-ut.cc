// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <gtest/gtest.h>

#include "ddprof_module.hpp"
#include "defer.hpp"
#include "dso_hdr.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_wrapper.hpp"
#include "loghandle.hpp"

#include <datadog/blazesym.h>
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

TEST(DwflModule, inconsistency_test) {
  blaze_symbolizer *symbolizer;
  constexpr blaze_symbolizer_opts opts{.type_size =
                                           sizeof(blaze_symbolizer_opts),
                                       .auto_reload = false,
                                       .code_info = false,
                                       .inlined_fns = false,
                                       .demangle = false,
                                       .reserved = {}};
  symbolizer = blaze_symbolizer_new_opts(&opts);
  pid_t my_pid = getpid();
  int nb_fds_start = count_fds(my_pid);
  printf("-- Start open file descriptors: %d\n", nb_fds_start);
  LogHandle handle;
  // Load DSOs from our unit test
  ElfAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  DsoHdr::DsoFindRes find_res = dso_hdr.dso_find_or_backpopulate(my_pid, ip);
  UniqueElf unique_elf = create_elf_from_self();
  // Check that we found the DSO matching this IP
  ASSERT_TRUE(find_res.second);
  {
    DwflWrapper dwfl_wrapper;
    dwfl_wrapper.attach(my_pid, unique_elf, nullptr);
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
      auto res = dwfl_wrapper.register_mod(dso._start, it->second,
                                           file_info_value, &ddprof_mod);

      ASSERT_TRUE(IsDDResOK(res));
      ASSERT_TRUE(ddprof_mod->_mod);
      if (find_res.first == it) {
        std::array<ElfAddress_t, 1> elf_addr{ip - ddprof_mod->_sym_bias};
        blaze_symbolize_src_elf src_elf{
            .type_size = sizeof(blaze_symbolize_src_elf),
            .path = dso._filename.c_str(),
            .debug_syms = true,
            .reserved = {},
        };
        const blaze_result *blaze_res = blaze_symbolize_elf_virt_offsets(
            symbolizer, &src_elf, elf_addr.data(), elf_addr.size());

        ASSERT_TRUE(blaze_res);
        ASSERT_GE(blaze_res->cnt, 1);
        defer { blaze_result_free(blaze_res); };
        // we don't have demangling at this step
        EXPECT_TRUE(
            strcmp("_ZN6ddprof34DwflModule_inconsistency_test_Test8TestBodyEv",
                   blaze_res->syms[0].name) == 0);
        // Only expect build-id on this binary (as we can not force it on
        // others)
        EXPECT_FALSE(ddprof_mod->_build_id.empty());
      }
      // check that we loaded all mods matching the DSOs
      EXPECT_EQ(ddprof_mod->_status, DDProfMod::kUnknown);
    }
  }
  blaze_symbolizer_free(symbolizer); //
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
    UniqueElf unique_elf = create_elf_from_self();
    DwflWrapper dwfl_wrapper;
    dwfl_wrapper.attach(child_pid, unique_elf, nullptr);
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
      auto res = dwfl_wrapper.register_mod(dso._start, it->second,
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
    UniqueElf unique_elf = create_elf_from_self();
    DwflWrapper dwfl_wrapper;
    dwfl_wrapper.attach(child_pid, unique_elf, nullptr);
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
      auto res = dwfl_wrapper.register_mod(dso._start, it->second,
                                           file_info_value, &ddprof_mod);
      ASSERT_TRUE(IsDDResOK(res));
      ASSERT_TRUE(ddprof_mod->_mod);
    }
  }
}

} // namespace ddprof
