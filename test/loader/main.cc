#include <dlfcn.h>
#include "CLI/CLI11.hpp"

#include "async-profiler/codeCache.h"
#include "async-profiler/symbols.h"

#include <execinfo.h>
#include <signal.h>

/*****************************  SIGSEGV Handler *******************************/
static void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
#ifdef __GLIBC__
  static void *buf[4096] = {0};
  size_t sz = backtrace(buf, 4096);
#endif
  fprintf(stderr, "loader[%d]: has encountered an error and will exit\n",
          getpid());
  if (sig == SIGSEGV)
    printf("Fault address: %p\n", si->si_addr);
#ifdef __GLIBC__
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
#endif
  exit(-1);
}

static void install_segfault_handler(){
  struct sigaction sigaction_handlers = {};
  sigaction_handlers.sa_sigaction = sigsegv_handler;
  sigaction_handlers.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &(sigaction_handlers), NULL);
}

int main(int argc, char *argv[]) {
  install_segfault_handler();

  CLI::App app{"Loads a library then tries to use async profiler"};

  //  Options opts;
  std::vector<std::string> exec_args;
  std::string lib_str;

  app.add_option("--lib", lib_str, "Library to open")->required()->check(CLI::ExistingFile);
  CLI11_PARSE(app, argc, argv);
  printf("Welcome to a library loader using the async profiler\n");
  void *handle = dlopen(lib_str.c_str(), RTLD_NOW);
  if (!handle) {
    printf("Error opening the library \n");
    exit(1);
  }
  {
    CodeCacheArray cache_arary;
    Symbols::parseLibraries(&cache_arary, false);
  }
  printf("Closing gracefully\n");
  dlclose(handle);
  return 0;
}
