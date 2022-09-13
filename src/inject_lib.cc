#include "inject_lib.hpp"

#include "ddres.hpp"
#include "logger.hpp"

#include "ModuleUtils/ReadLinuxModules.h"
#include "ModuleUtils/VirtualAndAbsoluteAddresses.h"
#include "ObjectUtils/ElfFile.h"
#include "OrbitBase/GetProcessIds.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/UniqueResource.h"
#include "UserSpaceInstrumentation/AccessTraceesMemory.h"
#include "UserSpaceInstrumentation/AddressRange.h"
#include "UserSpaceInstrumentation/AnyThreadIsInStrictSeccompMode.h"
#include "UserSpaceInstrumentation/Attach.h"
#include "UserSpaceInstrumentation/FindFunctionAddress.h"
#include "UserSpaceInstrumentation/InjectLibraryInTracee.h"
#include "UserSpaceInstrumentation/MachineCode.h"
#include "UserSpaceInstrumentation/Trampoline.h"

#include <capstone/capstone.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace orbit_user_space_instrumentation;

namespace {

using orbit_grpc_protos::ModuleInfo;

// MachineCodeForCloneCall creates the code to spawn a new thread inside the
// target process by using the clone syscall. This thread is used to execute the
// initialization code inside the target. Note that calling the result of the
// clone call a "thread" is a bit of a misnomer: We do not create a new data
// structure for thread local storage but use the one of the thread we halted.
ErrorMessageOr<MachineCode>
MachineCodeForCloneCall(pid_t pid, const std::vector<ModuleInfo> &modules,
                        void *library_handle, uint64_t top_of_stack) {
  constexpr uint64_t kCloneFlags = CLONE_FILES | CLONE_FS | CLONE_IO |
      CLONE_SIGHAND | CLONE_SYSVSEM | CLONE_THREAD | CLONE_VM;
  constexpr uint32_t kSyscallNumberClone = 0x38;
  constexpr uint32_t kSyscallNumberExit = 0x3c;
  constexpr const char *kInitializeInstrumentationFunctionName =
      "InitializeInstrumentation";
  OUTCOME_TRY(void *initialize_instrumentation_function_address,
              DlsymInTracee(pid, modules, library_handle,
                            kInitializeInstrumentationFunctionName));
  MachineCode code;
  code.AppendBytes({0x48, 0xbf})
      .AppendImmediate64(kCloneFlags) // mov rdi, kCloneFlags
      .AppendBytes({0x48, 0xbe})
      .AppendImmediate64(top_of_stack) // mov rsi, top_of_stack
      .AppendBytes({0x48, 0xba})
      .AppendImmediate64(0x0) // mov rdx, parent_tid
      .AppendBytes({0x49, 0xba})
      .AppendImmediate64(0x0) // mov r10, child_tid
      .AppendBytes({0x49, 0xb8})
      .AppendImmediate64(0x0)          // mov r8, tls
      .AppendBytes({0x48, 0xc7, 0xc0}) // mov rax, kSyscallNumberClone
      .AppendImmediate32(kSyscallNumberClone)
      .AppendBytes({0x0f, 0x05})                         // syscall (clone)
      .AppendBytes({0x48, 0x85, 0xc0})                   // testq	rax, rax
      .AppendBytes({0x0f, 0x84, 0x01, 0x00, 0x00, 0x00}) // jz 0x01(rip)
      .AppendBytes({0xcc})                               // int3
      .AppendBytes({0x48, 0xb8})
      .AppendImmediate64(absl::bit_cast<uint64_t>(
          initialize_instrumentation_function_address)) // mov rax,
                                                        // initialize_instrumentation
      .AppendBytes({0xff, 0xd0})                               // call rax
      .AppendBytes({0x48, 0xc7, 0xc7, 0x00, 0x00, 0x00, 0x00}) // mov rdi, 0x0
      .AppendBytes({0x48, 0xc7, 0xc0}) // mov rax, kSyscallNumberExit
      .AppendImmediate32(kSyscallNumberExit)
      .AppendBytes({0x0f, 0x05}); // syscall (exit)
  return code;
}

ErrorMessageOr<void> WaitForThreadToExit(pid_t pid, pid_t tid) {
  // In all tests the thread exited in one to three rounds of waiting for one
  // millisecond. To make sure that we never stall OrbitService here we return
  // an error when the thread requires an excessive amount of time to exit.
  constexpr int kNumberOfRetries = 3000;
  int count = 0;
  auto tids = orbit_base::GetTidsOfProcess(pid);
  while (std::find(tids.begin(), tids.end(), tid) != tids.end()) {
    if (count++ > kNumberOfRetries) {
      return ErrorMessage(
          "Initilization thread injected into target process failed to exit.");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
    tids = orbit_base::GetTidsOfProcess(pid);
  }
  return outcome::success();
}

ErrorMessageOr<void *>
inject_lib_internal(std::string_view lib_path, pid_t pid,
                    const std::vector<orbit_grpc_protos::ModuleInfo> &modules) {
  OUTCOME_TRY(orbit_user_space_instrumentation::AttachAndStopProcess(pid));
  orbit_base::unique_resource detach_on_exit_1{
      pid, [](int32_t pid2) {
        if (orbit_user_space_instrumentation::DetachAndContinueProcess(pid2)
                .has_error()) {
          ORBIT_ERROR("Detaching from %i", pid2);
        }
      }};

  if (AnyThreadIsInStrictSeccompMode(pid)) {
    return ErrorMessage(
        "At least one thread of the target process is in strict seccomp mode.");
  }
  // Inject library into target process.
  const std::filesystem::path library_path = lib_path;
  ORBIT_CHECK(library_path.is_absolute());
  ORBIT_LOG("Injecting library \"%s\" into process %d", library_path, pid);

  auto library_handle_or_error =
      orbit_user_space_instrumentation::DlopenInTracee(
          pid, modules, library_path, RTLD_NOW | RTLD_LOCAL);
  if (library_handle_or_error.has_error()) {
    return ErrorMessage(
        absl::StrFormat("Unable to open library in tracee: %s",
                        library_handle_or_error.error().message()));
  }
  void *library_handle = library_handle_or_error.value();

  //   constexpr const char *kEntryPayloadFunctionName = "EntryPayload";
  //   constexpr const char *kExitPayloadFunctionName = "ExitPayload";
  //   OUTCOME_TRY(void *entry_payload_function_address,
  //               orbit_user_space_instrumentation::DlsymInTracee(
  //                   pid, modules, library_handle,
  //                   kEntryPayloadFunctionName));
  //   entry_payload_function_address_ =
  //       absl::bit_cast<uint64_t>(entry_payload_function_address);
  //   OUTCOME_TRY(
  //       void *exit_payload_function_address,
  //       DlsymInTracee(pid, modules, library_handle,
  //       kExitPayloadFunctionName));
  //   exit_payload_function_address_ =
  //       absl::bit_cast<uint64_t>(exit_payload_function_address);

  //   // Get memory, create the return trampoline and make it executable.
  //   OUTCOME_TRY(auto &&return_trampoline_memory,
  //               MemoryInTracee::Create(pid, 0, GetReturnTrampolineSize()));
  //   return_trampoline_address_ = return_trampoline_memory->GetAddress();
  //   auto result = CreateReturnTrampoline(pid, exit_payload_function_address_,
  //                                        return_trampoline_address_);
  //   OUTCOME_TRY(return_trampoline_memory->EnsureMemoryExecutable());

  return library_handle;
}
} // namespace

namespace ddprof {
DDRes inject_library(std::string_view lib_path, pid_t pid) {
  auto modules_or_error = orbit_module_utils::ReadModules(pid);
  if (modules_or_error.has_error()) {
    LG_ERR("Failed to read modules for pid %d: %s", pid,
           modules_or_error.error().message().c_str());
    return ddres_error(DD_WHAT_UKNW);
  }
  auto lib_or_error =
      inject_lib_internal(lib_path, pid, modules_or_error.value());
  if (lib_or_error.has_error()) {
    printf("Failed to inject library: %s",
           lib_or_error.error().message().c_str());
    return ddres_error(DD_WHAT_UKNW);
  }
  return {};
}
} // namespace ddprof