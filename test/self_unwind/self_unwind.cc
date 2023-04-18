// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof.hpp"
#include "ddprof_context.hpp"
#include "ddprof_context_lib.hpp"
#include "ddprof_input.hpp"
#include "ddprof_output.hpp"
#include "ddres.hpp"
#include "stack_handler.hpp"
#include "stackchecker.hpp"

#include <iostream>
#include <sys/sysinfo.h>
#include <unistd.h>

static const char *k_test_executable = "BadBoggleSolver_run";

// Callback to give to ddprof
static bool stack_addtomap(const UnwindOutput *unwind_output,
                           const DDProfContext *ctx, void *callback_ctx,
                           int perf_option_pos);

namespace suw {

static pid_t launch_test_prog(void) {
  pid_t pid = fork();
  if (pid == -1) {
    // pid == -1 means error occured
    printf("can't fork, error occured\n");
    exit(EXIT_FAILURE);
  } else if (pid == 0) { // child test prog
    char *argv[] = {const_cast<char *>(k_test_executable),
                    const_cast<char *>("5"), (char *)NULL};
    int ret = execvp(argv[0], argv);
    exit(ret);
  } else { // parent
    return pid;
  }
}

void log_run_info(const SymbolMap &symbol_map) {
  std::cerr << "Number of symbols = " << symbol_map.size() << std::endl;
}

void capture_symbol(DDProfContext *ctx, SymbolMap *symbol_map) {
  const StackHandler stack_handler = {.apply = stack_addtomap,
                                      .callback_ctx =
                                          reinterpret_cast<void *>(symbol_map)};
  ddprof_attach_handler(ctx, &stack_handler);
}

int main(int argc, char *const argv[]) {
  DDProfInput input;
  int ret = 0;
  bool continue_exec;
  pid_t pid_test_prog = launch_test_prog();
  std::string data_directory("");
  const std::string exe_name(k_test_executable);

  if (argc >= 2) {
    data_directory = argv[1];
    std::cerr << "Override test data path with : " << data_directory
              << std::endl;
  }

  std::string str_pid = std::to_string(pid_test_prog);
  const char *argv_override[] = {MYNAME,    "--pid",     str_pid.c_str(),
                                 "--event", "sCPU,1000", (char *)NULL};
  SymbolMap symbol_map;
  DDProfContext ctx;

  // size - 1 as we add a null char at the end
  if (IsDDResNotOK(ddprof_input_parse(std::size(argv_override) - 1,
                                      const_cast<char **>(argv_override),
                                      &input, &continue_exec))) {
    std::cerr << "unable to init input " << std::endl;
    ret = -1;
    goto CLEANUP_INPUT;
  }
  if (!continue_exec) {
    std::cerr << "Bad arguments... EXIT" << std::endl;
    ret = -1;
    goto CLEANUP_INPUT;
  }

  if (IsDDResNotOK(ddprof_context_set(&input, &ctx))) {
    std::cerr << "unable to init input " << std::endl;
    ret = -1;
    goto CLEANUP;
  }
  // Launch profiling
  capture_symbol(&ctx, &symbol_map);

  log_run_info(symbol_map);
  // Capture new file (to help user create new reference)
  write_json_file(exe_name, symbol_map, data_directory);

  // Compare to reference stored in json file
  ret = compare_to_ref(exe_name, symbol_map, data_directory);

CLEANUP:
  ddprof_context_free(&ctx);
CLEANUP_INPUT:
  ddprof_input_free(&input);
  return ret;
}

} // namespace suw

int main(int argc, char *argv[]) { return suw::main(argc, argv); }

bool stack_addtomap(const UnwindOutput *unwind_output, const DDProfContext *ctx,
                    void *callback_ctx, int perf_option_pos) {
  // if it is not 0, things are strange
  assert(callback_ctx);
  suw::SymbolMap *symbol_map = reinterpret_cast<suw::SymbolMap *>(callback_ctx);
  assert(perf_option_pos == 0);
  for (unsigned i = 0; i < unwind_output->locs.size(); ++i) {
    const ddprof::Symbol &symbol = ddprof::get_symbol(ctx, unwind_output, i);
    if (symbol._demangle_name.find("0x") != std::string::npos) {
      // skip non symbolized frames
      continue;
    }
    suw::DwflSymbolKey key(symbol);
    (*symbol_map)[key] = symbol;
  }
  return true;
}
