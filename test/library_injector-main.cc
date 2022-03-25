#include "library_injector.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char*argv[]) {
    if (argc < 3) {
        printf("Usage: library_injector <pid> <library_path>\n");
        return 1;
    }
    pid_t pid = atoi(argv[1]);
    inject_library(pid, argv[2]);
}