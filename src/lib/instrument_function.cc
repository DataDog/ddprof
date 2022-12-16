// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* \fixme{nsavoire} This file is mostly a duplicate of
 * third_party/orbit/src/UserSpaceInstrumentation/InstrumentProcess.cpp modified
 * for ddprof use case. Instead of duplicating we could probably modify the
 * original file to adapt it. */

#include "instrument_function.hpp"

#include "ddres.hpp"
#include "logger.hpp"

#include "ModuleUtils/ReadLinuxModules.h"
#include "ModuleUtils/VirtualAndAbsoluteAddresses.h"
#include "ObjectUtils/ElfFile.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/UniqueResource.h"
#include "UserSpaceInstrumentation/AccessTraceesMemory.h"
#include "UserSpaceInstrumentation/AddressRange.h"
#include "UserSpaceInstrumentation/AnyThreadIsInStrictSeccompMode.h"
#include "UserSpaceInstrumentation/Attach.h"
#include "UserSpaceInstrumentation/FindFunctionAddress.h"
#include "UserSpaceInstrumentation/InjectLibraryInTracee.h"
#include "UserSpaceInstrumentation/Trampoline.h"
#include <absl/strings/str_split.h>

#include <capstone/capstone.h>

#include <cstdio>
#include <cstdlib>

using namespace orbit_user_space_instrumentation;

namespace {
class InstrumentedProcess {
public:
  InstrumentedProcess(uint64_t entry_address, uint64_t exit_address)
      : entry_payload_function_address_(entry_address),
        exit_payload_function_address_(exit_address) {}

  uint64_t entry_payload_function_address_ = 0;
  uint64_t return_trampoline_address_ = 0;
  uint64_t exit_payload_function_address_ = 0;

  static constexpr int kTrampolinesPerChunk = 4096;
  struct TrampolineMemoryChunk {
    TrampolineMemoryChunk() = default;
    TrampolineMemoryChunk(std::unique_ptr<MemoryInTracee> m,
                          int first_available)
        : memory(std::move(m)), first_available(first_available) {}
    std::unique_ptr<MemoryInTracee> memory;
    int first_available = 0;
  };
  using TrampolineMemoryChunks = std::vector<TrampolineMemoryChunk>;
  absl::flat_hash_map<AddressRange, TrampolineMemoryChunks>
      trampolines_for_modules_;

  absl::flat_hash_map<uint64_t, uint64_t> relocation_map_;

  // Keep track of all trampolines we created for this process.
  struct TrampolineData {
    uint64_t trampoline_address;
    uint64_t address_after_prologue;
    // The first few bytes of the function. Guaranteed to contain everything
    // that was overwritten.
    std::vector<uint8_t> function_data;
  };
  // Maps function addresses to TrampolineData.
  absl::flat_hash_map<uint64_t, TrampolineData> trampoline_map_;

  ErrorMessageOr<void>
  ReleaseMostRecentlyAllocatedTrampolineMemory(AddressRange address_range) {
    if (!trampolines_for_modules_.contains(address_range)) {
      return ErrorMessage("Tried to release trampoline memory for a non "
                          "existent address range");
    }
    trampolines_for_modules_.find(address_range)
        ->second.back()
        .first_available--;
    return outcome::success();
  }

  ErrorMessageOr<void> EnsureTrampolinesWritable() {
    for (auto &trampoline_for_module : trampolines_for_modules_) {
      for (auto &memory_chunk : trampoline_for_module.second) {
        OUTCOME_TRY(memory_chunk.memory->EnsureMemoryWritable());
      }
    }
    return outcome::success();
  }

  ErrorMessageOr<void> EnsureTrampolinesExecutable() {
    for (auto &trampoline_for_module : trampolines_for_modules_) {
      for (auto &memory_chunk : trampoline_for_module.second) {
        OUTCOME_TRY(memory_chunk.memory->EnsureMemoryExecutable());
      }
    }
    return outcome::success();
  }

  ErrorMessageOr<uint64_t> GetTrampolineMemory(pid_t pid,
                                               AddressRange address_range) {
    if (!trampolines_for_modules_.contains(address_range)) {
      trampolines_for_modules_.emplace(address_range, TrampolineMemoryChunks());
    }
    auto it = trampolines_for_modules_.find(address_range);
    TrampolineMemoryChunks &trampoline_memory_chunks = it->second;
    if (trampoline_memory_chunks.empty() ||
        trampoline_memory_chunks.back().first_available ==
            kTrampolinesPerChunk) {
      OUTCOME_TRY(auto &&trampoline_memory,
                  AllocateMemoryForTrampolines(pid, address_range,
                                               kTrampolinesPerChunk *
                                                   GetMaxTrampolineSize()));

      trampoline_memory_chunks.emplace_back(std::move(trampoline_memory), 0);
    }
    const uint64_t result =
        trampoline_memory_chunks.back().memory->GetAddress() +
        trampoline_memory_chunks.back().first_available *
            GetMaxTrampolineSize();
    trampoline_memory_chunks.back().first_available++;
    return result;
  }

  ErrorMessageOr<void> instrument_function_internal(
      pid_t pid, std::string_view function_name, uint64_t function_id,
      std::string_view module_path,
      const std::vector<orbit_grpc_protos::ModuleInfo> &modules) {
    ORBIT_LOG("Instrumenting functions in process %d", pid);
    // \fixme{nsavoire}:
    // Instrumentation is done from the process itself, no need to attach/stop.
    // In a multithreaded process, this could cause issues if instruction
    // pointer from another thread points to a code location that is overwritten
    // by instrumentation

    //   OUTCOME_TRY(AttachAndStopProcess(pid));
    //   orbit_base::unique_resource detach_on_exit{
    //       pid, [](int32_t pid2) {
    //         if (DetachAndContinueProcess(pid2).has_error()) {
    //           ORBIT_ERROR("Detaching from %i", pid2);
    //         }
    //       }};

    if (return_trampoline_address_ == 0) {
      OUTCOME_TRY(auto &&return_trampoline_memory,
                  MemoryInTracee::Create(pid, 0, GetReturnTrampolineSize()));
      return_trampoline_address_ = return_trampoline_memory->GetAddress();
      auto result = CreateReturnTrampoline(pid, exit_payload_function_address_,
                                           return_trampoline_address_);
      OUTCOME_TRY(return_trampoline_memory->EnsureMemoryExecutable());
    }

    // Init Capstone disassembler.
    csh capstone_handle = 0;
    cs_err error_code = cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle);
    if (error_code != CS_ERR_OK) {
      return ErrorMessage(absl::StrFormat(
          "Failed to open Capstone disassembler [%d].", error_code));
    }
    error_code = cs_option(capstone_handle, CS_OPT_DETAIL, CS_OPT_ON);
    if (error_code != CS_ERR_OK) {
      return ErrorMessage("Failed to configure Capstone disassembler.");
    }
    orbit_base::unique_resource close_on_exit{
        &capstone_handle,
        [](csh *capstone_handle) { cs_close(capstone_handle); }};

    const orbit_grpc_protos::ModuleInfo *mod_info;
    orbit_grpc_protos::SymbolInfo sym_info;
    uint64_t function_address = 0;

    // Lookup function in loaded modules
    // \fixme{nsavoire} if module_path is empty, function is looked up in all
    // modules, but only the last match is instrumented
    for (const auto &module : modules) {
      if ((module_path.empty() &&
           // hacky: avoid instrumenting ourselves
           module.file_path().find("/libdd_profiling") == std::string::npos) ||
          module_path == module.file_path()) {
        OUTCOME_TRY(auto &&elf_file,
                    orbit_object_utils::CreateElfFile(module.file_path()));
        if (elf_file->HasDebugSymbols()) {
          OUTCOME_TRY(auto &&symbols, elf_file->LoadDebugSymbols());
          for (const auto &sym : symbols.symbol_infos()) {
            if (sym.demangled_name() == function_name) {
              ORBIT_LOG("Found function %s in %s\n", function_name,
                        module.file_path());
              function_address =
                  orbit_module_utils::SymbolVirtualAddressToAbsoluteAddress(
                      sym.address(), module.address_start(), module.load_bias(),
                      module.executable_segment_offset());
              ORBIT_LOG("Found function %s in %s at 0x%lx[0x%lx]\n",
                        function_name, module.file_path(), sym.address(),
                        function_address);
              mod_info = &module;
              sym_info = sym;
              break;
            }
          }
        }
        if (function_address == 0 && elf_file->HasDynsym()) {
          OUTCOME_TRY(
              auto &&symbols,
              elf_file->LoadDynamicLinkingSymbolsAndUnwindRangesAsSymbols());
          for (const auto &sym : symbols.symbol_infos()) {
            if (sym.demangled_name() == function_name) {

              function_address =
                  orbit_module_utils::SymbolVirtualAddressToAbsoluteAddress(
                      sym.address(), module.address_start(), module.load_bias(),
                      module.executable_segment_offset());
              ORBIT_LOG("Found function %s in %s at 0x%lx[0x%lx]\n",
                        function_name, module.file_path(), sym.address(),
                        function_address);
              mod_info = &module;
              sym_info = sym;
              break;
            }
          }
        }
      }
    }

    if (function_address == 0) {
      return ErrorMessage("Failed to find function.");
    }
    const AddressRange module_address_range(mod_info->address_start(),
                                            mod_info->address_end());
    auto trampoline_address_or_error =
        GetTrampolineMemory(pid, module_address_range);
    if (trampoline_address_or_error.has_error()) {
      ORBIT_ERROR("Failed to allocate memory for trampoline: %s",
                  trampoline_address_or_error.error().message());
      return trampoline_address_or_error.error();
    }
    const uint64_t trampoline_address = trampoline_address_or_error.value();
    // We need the machine code of the function for two purposes: We need to
    // relocate the instructions that get overwritten into the trampoline and we
    // also need to check if the function contains a jump back into the first
    // five bytes (which would prohibit instrumentation). For the first reason
    // 20 bytes would be enough; the 200 is chosen somewhat arbitrarily to cover
    // all cases of jumps into the first five bytes we encountered in the wild.
    // Specifically this covers all relative jumps to a signed 8 bit offset.
    // Compare the comment of CheckForRelativeJumpIntoFirstFiveBytes in
    // Trampoline.cpp.
    constexpr uint64_t kMaxFunctionReadSize = 200;
    const uint64_t function_read_size =
        std::min(kMaxFunctionReadSize, sym_info.size());
    OUTCOME_TRY(auto &&function_data,
                ReadTraceesMemory(pid, function_address, function_read_size));
    auto address_after_prologue_or_error = CreateTrampoline(
        pid, function_address, function_data, trampoline_address,
        entry_payload_function_address_, return_trampoline_address_,
        capstone_handle, relocation_map_);
    if (address_after_prologue_or_error.has_error()) {
      const std::string message = absl::StrFormat(
          "Can't instrument function \"%s\". Failed to create trampoline: %s",
          function_name, address_after_prologue_or_error.error().message());
      ORBIT_ERROR("%s", message);
      OUTCOME_TRY(
          ReleaseMostRecentlyAllocatedTrampolineMemory(module_address_range));
      return address_after_prologue_or_error.error();
    }
    OUTCOME_TRY(EnsureTrampolinesExecutable());
    TrampolineData trampoline_data;
    trampoline_data.trampoline_address = trampoline_address;
    // We'll overwrite the first five bytes of the function and the rest of the
    // instruction that we clobbered. Since we'll need to restore that when we
    // remove the instrumentation we need a backup.
    constexpr uint64_t kMaxFunctionBackupSize = 20;
    const uint64_t function_backup_size =
        std::min(kMaxFunctionBackupSize, sym_info.size());
    OUTCOME_TRY(auto &&function_backup_data,
                ReadTraceesMemory(pid, function_address, function_backup_size));
    trampoline_data.function_data = function_backup_data;
    trampoline_data.address_after_prologue =
        address_after_prologue_or_error.value();
    trampoline_map_.emplace(function_address, trampoline_data);
    auto result_or_error =
        InstrumentFunction(pid, function_address, function_id,
                           trampoline_data.address_after_prologue,
                           trampoline_data.trampoline_address);
    if (result_or_error.has_error()) {
      const std::string message = absl::StrFormat(
          "Can't instrument function \"%s\": %s", sym_info.demangled_name(),
          result_or_error.error().message());
      ORBIT_ERROR("%s", message);
      // result.function_ids_to_error_messages[function_id] = message;
    } else {
      // addresses_of_instrumented_functions_.insert(function_address);
      // result.instrumented_function_ids.insert(function_id);
    }

    // \fixme{nsavoire} Don't instrumentation pointers when
    // instrumentation is done from process itself.

    // MoveInstructionPointersOutOfOverwrittenCode(pid, relocation_map_);

    // OUTCOME_TRY(EnsureTrampolinesExecutable());

    return outcome::success();
  }
};

InstrumentedProcess &getInstrumentedProcess() {
  static InstrumentedProcess p((uint64_t)&EntryPayload, (uint64_t)&ExitPayload);
  return p;
}
} // namespace

namespace ddprof {
DDRes instrument_function(std::string_view function_name,
                          uint64_t function_id) {
  auto modules_or_error = orbit_module_utils::ReadModules(getpid());
  if (modules_or_error.has_error()) {
    LG_ERR("Failed to read modules: %s",
           modules_or_error.error().message().c_str());
    return ddres_error(DD_WHAT_UKNW);
  }
  auto &p = getInstrumentedProcess();
  auto res = p.instrument_function_internal(0, function_name, function_id, {},
                                            modules_or_error.value());
  if (res.has_error()) {
    LG_ERR("Failed to instrument function %s: %s",
           std::string(function_name).c_str(), res.error().message().c_str());
    return ddres_error(DD_WHAT_UKNW);
  }

  return {};
}
} // namespace ddprof