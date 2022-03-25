#pragma once
#include <sys/types.h>

int inject_library(pid_t pid, const char* lib_path);