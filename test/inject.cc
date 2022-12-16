#include "inject_library.hpp"
#include "instrument_function.hpp"

#include <cstdio>
#include <cstdlib>

// \fixme{nsavoire} dummy defs
void EntryPayload(uint64_t return_address, uint64_t function_id,
                  uint64_t stack_pointer, uint64_t return_trampoline_address) {}

uint64_t ExitPayload() { return 0; }

int main(int argc, char *argv[]) {
  if (argc < 4) {
    printf("Usage: inject <pid> <library_path> <func_name>\n");
    return 1;
  }

  pid_t pid = atoi(argv[1]);
  std::string_view lib_path{argv[2]};
  std::string_view func_name{argv[3]};

  uint64_t entry_payload_address = 0, exit_payload_address = 0;
  auto res = ddprof::inject_library(lib_path, pid, entry_payload_address,
                                    exit_payload_address);
  if (!IsDDResOK(res)) {
    printf("Failed to inject library\n");
    return 1;
  }
  auto res2 = ddprof::instrument_function(
      pid, func_name, 1, entry_payload_address, exit_payload_address);
  if (!IsDDResOK(res2)) {
    printf("Failed to instrument function\n");
    return 1;
  }
}
