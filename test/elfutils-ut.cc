// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "elfutils.hpp"

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <memory>
#include <span>

namespace ddprof {

TEST(elfutils, lookup_symbol_with_sysv_hash_only) {
  auto close_library = [](void *handle) { dlclose(handle); };
  std::unique_ptr<void, decltype(close_library)> library{
      dlopen(ELFUTILS_SYSV_FIXTURE, RTLD_NOW | RTLD_LOCAL), close_library};
  ASSERT_NE(library, nullptr) << dlerror();

  const auto *expected = dlsym(library.get(), "ddprof_elfutils_sysv_fixture");
  ASSERT_NE(expected, nullptr) << dlerror();

  bool found_library = false;
  bool has_sysv_hash = false;
  bool has_gnu_hash = false;
  iterate_over_loaded_libraries([&](const dl_phdr_info &info, bool) {
    if (std::string_view{info.dlpi_name} != ELFUTILS_SYSV_FIXTURE) {
      return LibraryCallbackStatus::Continue;
    }
    found_library = true;
    for (const auto &phdr : std::span{info.dlpi_phdr, info.dlpi_phnum}) {
      if (phdr.p_type != PT_DYNAMIC) {
        continue;
      }
      const auto *dynamic =
          reinterpret_cast<const ElfW(Dyn) *>(info.dlpi_addr + phdr.p_vaddr);
      for (const auto &entry :
           std::span{dynamic, phdr.p_memsz / sizeof(ElfW(Dyn))}) {
        if (entry.d_tag == DT_NULL) {
          break;
        }
        has_sysv_hash |= entry.d_tag == DT_HASH;
        has_gnu_hash |= entry.d_tag == DT_GNU_HASH;
      }
    }
    return LibraryCallbackStatus::Stop;
  });
  ASSERT_TRUE(found_library);
  ASSERT_TRUE(has_sysv_hash);
  ASSERT_FALSE(has_gnu_hash);

  const auto result = lookup_symbol("ddprof_elfutils_sysv_fixture", false);
  EXPECT_EQ(result.symbol.st_value, reinterpret_cast<uintptr_t>(expected));
  EXPECT_GT(result.symbol.st_size, 0u);
  EXPECT_EQ(result.object_name, ELFUTILS_SYSV_FIXTURE);
}

} // namespace ddprof
