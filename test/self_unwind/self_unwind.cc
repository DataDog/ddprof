extern "C" {
#include "ddprof.h"
#include "ddprof_context.h"
#include "ddprof_input.h"
#include "stack_handler.h"

#include <sys/sysinfo.h>
#include <unistd.h>
}

#include "ddprof_output.hpp"
#include "ddres.h"
#include "stackchecker.hpp"

#include <iostream>
#include <unistd.h>

#define SZ_STATIC_ARRAY(array) (sizeof(array) / sizeof(array[0]))

static const char *k_test_executable = "BadBoggleSolver_run";

extern "C" {
// Callback to give to ddprof
static bool stack_addtomap(const UnwindOutput *unwind_output,
                           const DDProfContext *ctx, void *callback_ctx,
                           int perf_option_pos);
}

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

void capture_symbol(DDProfContext *ctx, SymbolMap *ip_info_map) {
  const StackHandler stack_handler = {
      .apply = stack_addtomap,
      .callback_ctx = reinterpret_cast<void *>(ip_info_map)};
  ddprof_attach_handler(ctx, &stack_handler);
}

int main(int argc, char *argv[]) {
  DDProfInput input;
  int ret = 0;
  bool continue_exec;
  pid_t pid_test_prog = launch_test_prog();
  std::string_view data_directory("");

  if (argc >= 2) {
    data_directory = argv[1];
    std::cerr << "Override test data path with : " << data_directory
              << std::endl;
  }

  std::string str_pid = std::to_string(pid_test_prog);
  const char *argv_override[] = {MYNAME,    "--pid",     str_pid.c_str(),
                                 "--event", "sCPU,1000", (char *)NULL};
  SymbolMap ip_info_map;

  // size - 1 as we add a null char at the end
  if (IsDDResNotOK(ddprof_input_parse(SZ_STATIC_ARRAY(argv_override) - 1,
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

  DDProfContext ctx;

  if (IsDDResNotOK(ddprof_ctx_set(&input, &ctx))) {
    std::cerr << "unable to init input " << std::endl;
    ret = -1;
    goto CLEANUP;
  }
  // Launch profiling
  capture_symbol(&ctx, &ip_info_map);

  // Compare to reference stored in json file
  ret = compare_to_ref(k_test_executable, ip_info_map, data_directory);

  // Capture new file (to help user create new reference)
  write_json_file(k_test_executable, ip_info_map, data_directory);

CLEANUP:
  ddprof_ctx_free(&ctx);
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
  suw::SymbolMap *ip_info_map =
      reinterpret_cast<suw::SymbolMap *>(callback_ctx);
  assert(perf_option_pos == 0);
  for (unsigned i = 0; i < unwind_output->nb_locs; ++i) {
    const ddprof::Symbol &ip_info = ddprof::get_symbol(ctx, unwind_output, i);
    suw::DwflSymbolKey key(ip_info);
    (*ip_info_map)[key] = ip_info;
  }
  return true;
}
