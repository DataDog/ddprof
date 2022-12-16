#pragma once

#include "ddres_def.hpp"

#include <cstdint>
#include <string_view>
#include <unistd.h>

// Payload called on entry of an instrumented function. Needs to record the
// return address of the function (in order to have it available in
// `ExitPayload`) and the stack pointer (i.e., the address of the return
// address). `function_id` is the id of the instrumented function. Also needs to
// overwrite the return address stored at `stack_pointer` with the
// `return_trampoline_address`.
extern "C" void EntryPayload(uint64_t return_address, uint64_t function_id,
                             uint64_t stack_pointer,
                             uint64_t return_trampoline_address);

// Payload called on exit of an instrumented function. Needs to return the
// actual return address of the function such that the execution can be
// continued there.
extern "C" uint64_t ExitPayload();

namespace ddprof {
DDRes instrument_function(std::string_view function_name, uint64_t function_id);
DDRes instrument_function(pid_t pid, std::string_view function_name,
                          uint64_t function_id, uint64_t entry_payload_address,
                          uint64_t exit_payload_address);

} // namespace ddprof
