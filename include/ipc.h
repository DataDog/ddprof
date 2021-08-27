#pragma once

#include <stdbool.h>

#define DDPF_MAX_FD 253

bool sendfd(int sfd, int *fd, int sz);

// TODO right now this allocates memory which has to be freed by the caller,
//      maybe we can do a bit better with some refactoring.
int *getfd(int sfd, int *sz);
